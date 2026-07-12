// keyboardTD — SSH frontend: `ssh td.zboina.pl` drops you straight into the
// game, terminal.shop style. Any username, no password. Each session gets
// the ncurses binary on a fresh PTY in its own temp directory, and a pair of
// extra pipes (fds 3/4, see platform_ncurses.cpp) bridges hall-of-fame
// traffic to the same API the web build uses — this server does the HTTP,
// stamping the SSH client's IP into X-Real-IP so the API's per-IP rate
// limits apply to the actual player.
//
// Abuse limits: global and per-IP concurrent session caps, an input idle
// timeout (rendering output doesn't count — only keystrokes), and a hard
// session length cap. All tunable via KTD_SSH_* env vars.
package main

import (
	"bufio"
	"bytes"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/creack/pty"
	"github.com/gliderlabs/ssh"
	gossh "golang.org/x/crypto/ssh"
)

func env(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

func envInt(key string, def int) int {
	var v int
	if _, err := fmt.Sscanf(os.Getenv(key), "%d", &v); err == nil && v > 0 {
		return v
	}
	return def
}

func envDur(key string, def time.Duration) time.Duration {
	if d, err := time.ParseDuration(os.Getenv(key)); err == nil && d > 0 {
		return d
	}
	return def
}

var (
	addr        = env("KTD_SSH_ADDR", ":5555")
	hostKeyPath = env("KTD_SSH_HOSTKEY", "/data/ssh_host_ed25519_key")
	gameBin     = env("KTD_GAME", "/app/keyboardtd")
	apiBase     = env("KTD_API", "http://127.0.0.1:8081")
	maxSessions = envInt("KTD_SSH_MAX_SESSIONS", 40)
	maxPerIP    = envInt("KTD_SSH_MAX_PER_IP", 3)
	idleTimeout = envDur("KTD_SSH_IDLE_TIMEOUT", 5*time.Minute)
	maxSession  = envDur("KTD_SSH_MAX_SESSION", 2*time.Hour)
)

// ---- concurrent-session limits ----------------------------------------------

type limiter struct {
	mu    sync.Mutex
	total int
	perIP map[string]int
}

func newLimiter() *limiter { return &limiter{perIP: map[string]int{}} }

// acquire reserves a session slot; the returned reason is non-empty when
// the connection should be refused instead.
func (l *limiter) acquire(ip string) string {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.total >= maxSessions {
		return "the arena is full right now — try again in a few minutes"
	}
	if l.perIP[ip] >= maxPerIP {
		return "too many open sessions from your address"
	}
	l.total++
	l.perIP[ip]++
	return ""
}

func (l *limiter) release(ip string) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.total--
	if l.perIP[ip] <= 1 {
		delete(l.perIP, ip)
	} else {
		l.perIP[ip]--
	}
}

// ---- hall-of-fame bridge ----------------------------------------------------

type scoreEntry struct {
	Nick  string `json:"nick"`
	Score int64  `json:"score"`
}

// board formats entries the way gameSetScores wants them: "nick score"
// lines, best first, closed by a lone "." so the game knows the blob ended.
func board(entries []scoreEntry) []byte {
	var b bytes.Buffer
	for _, e := range entries {
		fmt.Fprintf(&b, "%s %d\n", e.Nick, e.Score)
	}
	b.WriteString(".\n")
	return b.Bytes()
}

func fetchTop10(client *http.Client) []byte {
	resp, err := client.Get(apiBase + "/api/top10")
	if err != nil {
		return nil
	}
	defer resp.Body.Close()
	var entries []scoreEntry
	if json.NewDecoder(io.LimitReader(resp.Body, 64<<10)).Decode(&entries) != nil {
		return nil
	}
	return board(entries)
}

// submit relays "SUBMIT <nick> <score> <wpm> <level> <duration>" to the API.
// The API re-validates everything; the client IP travels in X-Real-IP so its
// per-IP submission limits hit the player, not this proxy.
func submit(client *http.Client, ip, line string) []byte {
	f := strings.Fields(line)
	if len(f) != 6 {
		return nil
	}
	payload := map[string]any{"nick": f[1]}
	for i, key := range []string{"score", "wpm", "level", "duration"} {
		var v int64
		if _, err := fmt.Sscanf(f[i+2], "%d", &v); err != nil {
			return nil
		}
		payload[key] = v
	}
	body, _ := json.Marshal(payload)
	req, err := http.NewRequest("POST", apiBase+"/api/score", bytes.NewReader(body))
	if err != nil {
		return nil
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-Real-IP", ip)
	resp, err := client.Do(req)
	if err != nil {
		return nil
	}
	defer resp.Body.Close()
	var r struct {
		Top10 []scoreEntry `json:"top10"`
	}
	if json.NewDecoder(io.LimitReader(resp.Body, 64<<10)).Decode(&r) != nil {
		return nil
	}
	if r.Top10 == nil {
		return nil // rejected submission; leave the on-screen board alone
	}
	return board(r.Top10)
}

// bridge services the game's command pipe until the game exits. Errors are
// swallowed: a failed fetch just means the board stays empty.
func bridge(cmdR io.Reader, dataW io.Writer, ip string) {
	sc := bufio.NewScanner(cmdR)
	sc.Buffer(make([]byte, 256), 256)
	client := &http.Client{Timeout: 5 * time.Second}
	for sc.Scan() {
		var reply []byte
		switch line := sc.Text(); {
		case line == "FETCH":
			reply = fetchTop10(client)
		case strings.HasPrefix(line, "SUBMIT "):
			reply = submit(client, ip, line)
		}
		if reply != nil {
			if _, err := dataW.Write(reply); err != nil {
				return
			}
		}
	}
}

// ---- session handling -------------------------------------------------------

var termRe = regexp.MustCompile(`^[A-Za-z0-9+._-]{1,32}$`)

func clientIP(s ssh.Session) string {
	if host, _, err := net.SplitHostPort(s.RemoteAddr().String()); err == nil {
		return host
	}
	return s.RemoteAddr().String()
}

func handle(s ssh.Session, limits *limiter) {
	ip := clientIP(s)

	if len(s.Command()) > 0 {
		fmt.Fprintf(s, "no commands here — just `ssh %s` and play.\n", "td.zboina.pl")
		s.Exit(1)
		return
	}
	ptyReq, winCh, isPty := s.Pty()
	if !isPty {
		fmt.Fprintf(s, "keyboardTD needs a terminal; try `ssh -t`.\n")
		s.Exit(1)
		return
	}
	if reason := limits.acquire(ip); reason != "" {
		fmt.Fprintf(s, "%s\n", reason)
		s.Exit(1)
		return
	}
	defer limits.release(ip)

	// Fresh working dir per session: highscore.txt / theme.txt land here and
	// vanish with the session (the persistent board is the online one).
	dir, err := os.MkdirTemp("", "ktd-*")
	if err != nil {
		fmt.Fprintf(s, "server hiccup, sorry.\n")
		s.Exit(1)
		return
	}
	defer os.RemoveAll(dir)

	term := ptyReq.Term
	if !termRe.MatchString(term) {
		term = "xterm-256color"
	}

	// Bridge pipes: the game writes commands to fd 3, reads boards from fd 4.
	cmdR, cmdW, err1 := os.Pipe()
	dataR, dataW, err2 := os.Pipe()
	if err1 != nil || err2 != nil {
		fmt.Fprintf(s, "server hiccup, sorry.\n")
		s.Exit(1)
		return
	}

	cmd := exec.Command(gameBin)
	cmd.Dir = dir
	cmd.Env = []string{
		"TERM=" + term,
		"HOME=" + dir,
		"LANG=C.UTF-8",
		"KTD_LEADERBOARD=1",
	}
	cmd.ExtraFiles = []*os.File{cmdW, dataR} // child fds 3, 4

	f, err := pty.StartWithSize(cmd, &pty.Winsize{
		Rows: uint16(ptyReq.Window.Height),
		Cols: uint16(ptyReq.Window.Width),
	})
	// Parent copies of the child's pipe ends must close regardless.
	cmdW.Close()
	dataR.Close()
	if err != nil {
		cmdR.Close()
		dataW.Close()
		log.Printf("[%s] failed to start game: %v", ip, err)
		fmt.Fprintf(s, "server hiccup, sorry.\n")
		s.Exit(1)
		return
	}
	defer f.Close()
	defer cmdR.Close()
	defer dataW.Close()

	log.Printf("[%s] session start (user %q, term %s, %dx%d)",
		ip, s.User(), term, ptyReq.Window.Width, ptyReq.Window.Height)
	start := time.Now()
	var lastInput atomic.Int64
	lastInput.Store(start.UnixNano())
	done := make(chan struct{})

	go bridge(cmdR, dataW, ip)

	go func() { // window resizes
		for win := range winCh {
			pty.Setsize(f, &pty.Winsize{Rows: uint16(win.Height), Cols: uint16(win.Width)})
		}
	}()

	go func() { // keystrokes; a read error means the client went away
		buf := make([]byte, 1024)
		for {
			n, err := s.Read(buf)
			if n > 0 {
				lastInput.Store(time.Now().UnixNano())
				if _, werr := f.Write(buf[:n]); werr != nil {
					break
				}
			}
			if err != nil {
				break
			}
		}
		cmd.Process.Kill()
	}()

	go func() { // idle / max-length watchdog
		tick := time.NewTicker(15 * time.Second)
		defer tick.Stop()
		for {
			select {
			case <-done:
				return
			case <-tick.C:
				idle := time.Since(time.Unix(0, lastInput.Load()))
				if idle > idleTimeout {
					fmt.Fprintf(s, "\r\n\r\nidle for %s — freeing the slot. come back soon!\r\n", idle.Round(time.Second))
					cmd.Process.Kill()
					return
				}
				if time.Since(start) > maxSession {
					fmt.Fprintf(s, "\r\n\r\nsession cap (%s) reached — reconnect to keep playing.\r\n", maxSession)
					cmd.Process.Kill()
					return
				}
			}
		}
	}()

	io.Copy(s, f) // game output -> client; ends when the game exits
	close(done)
	cmd.Process.Kill()
	cmd.Wait()
	log.Printf("[%s] session end after %s", ip, time.Since(start).Round(time.Second))
	s.Exit(0)
}

// ---- host key ---------------------------------------------------------------

// ensureHostKey loads the ed25519 host key, generating and persisting one on
// first boot (it lives on the /data volume so redeploys keep the identity).
func ensureHostKey(path string) ([]byte, error) {
	if pemBytes, err := os.ReadFile(path); err == nil {
		return pemBytes, nil
	}
	_, priv, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return nil, err
	}
	block, err := gossh.MarshalPrivateKey(priv, "keyboardtd host key")
	if err != nil {
		return nil, err
	}
	pemBytes := pem.EncodeToMemory(block)
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return nil, err
	}
	if err := os.WriteFile(path, pemBytes, 0o600); err != nil {
		return nil, err
	}
	return pemBytes, nil
}

func main() {
	log.SetPrefix("ktd-ssh: ")
	log.SetFlags(log.LstdFlags)

	pemBytes, err := ensureHostKey(hostKeyPath)
	if err != nil {
		log.Fatalf("host key: %v", err)
	}

	limits := newLimiter()
	srv := &ssh.Server{
		Addr:    addr,
		Handler: func(s ssh.Session) { handle(s, limits) },
		// No auth handlers = accept everyone ("none" auth): any username,
		// no password prompt. Forwarding and subsystems stay disabled.
		Version: "keyboardTD",
		// Backstop for stalled connections; gameplay limits are enforced
		// per-keystroke by the watchdog above.
		MaxTimeout: maxSession + 10*time.Minute,
	}
	ssh.HostKeyPEM(pemBytes)(srv)

	log.Printf("listening on %s (game %s, api %s, max %d sessions / %d per IP, idle %s, cap %s)",
		addr, gameBin, apiBase, maxSessions, maxPerIP, idleTimeout, maxSession)
	log.Fatal(srv.ListenAndServe())
}

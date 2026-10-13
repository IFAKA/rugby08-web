package main

// playlog.go -- every player's game log, kept on the server's machine (local files only) so a play test
// can be read back afterwards: the page posts its game's trace lines (Module.print) every few seconds
// to /log?id=ID (ID: the page's own, made once per page load), appended to LOGDIR/<start>-<ID>.log.

import (
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sync"
	"time"
)

var (
	playLogDir   string
	playLogMu    sync.Mutex
	playLogFiles = map[string]string{} // page id -> file
	idPattern    = regexp.MustCompile(`^[a-z0-9]{4,32}$`)
)

const playLogMax = 512 << 20 // per file

func servePlayLog(w http.ResponseWriter, r *http.Request) {
	id := r.URL.Query().Get("id")
	if r.Method != http.MethodPost || !idPattern.MatchString(id) || playLogDir == "" {
		http.Error(w, "bad log post", http.StatusBadRequest)
		return
	}
	body, err := io.ReadAll(io.LimitReader(r.Body, 8<<20))
	if err != nil {
		return
	}
	playLogMu.Lock()
	defer playLogMu.Unlock()
	name, ok := playLogFiles[id]
	if !ok {
		name = filepath.Join(playLogDir, time.Now().Format("20060102-150405")+"-"+id+".log")
		playLogFiles[id] = name
		log.Printf("play log %s", name)
	}
	if st, err := os.Stat(name); err == nil && st.Size() > playLogMax {
		return
	}
	f, err := os.OpenFile(name, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		log.Printf("play log: %v", err)
		return
	}
	defer f.Close()
	f.Write(body)
	w.WriteHeader(http.StatusNoContent)
}

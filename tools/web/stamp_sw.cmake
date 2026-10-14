# stamp_sw.cmake -- the service worker with this build's id (platform/web/pwa/sw.js '@BUILD@'): each build
# is a new worker, so an installed app updates itself (cmake -DIN=sw.js -DOUT=build/web/sw.js -P this).
string(TIMESTAMP id "%Y%m%d%H%M%S" UTC)
file(READ "${IN}" sw)
string(REPLACE "@BUILD@" "${id}" sw "${sw}")
file(WRITE "${OUT}" "${sw}")

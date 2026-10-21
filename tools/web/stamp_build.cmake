# stamp_build.cmake -- index.html = the linked page with this build's id in place of @RUGBY_BUILD@ (a
# lockstep party plays one build: the server refuses a page of another build, server/main.go).
# cmake -DIN=rugby08.html -DOUT=index.html -P stamp_build.cmake
file(READ ${IN} page)
string(TIMESTAMP now "%Y%m%d%H%M%S" UTC)
string(RANDOM LENGTH 6 salt)
string(REPLACE "@RUGBY_BUILD@" "${now}-${salt}" page "${page}")
file(WRITE ${OUT} "${page}")
message(STATUS "index.html: build ${now}-${salt}")

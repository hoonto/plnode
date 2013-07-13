plnode
===

### Evented I/O for Javascript via Node.js stuffed straight up Postgres' sweet PL.

git clone -b  v0.10.12-plnode-0.0.1  https://github.com/hoonto/plnode.git

This is merely an edge effort.  An effort to add yet another edge to the rapid evolution of web-oriented frameworks.

More specifically, I began this fork in order to investigate taking the M in MV* and put it inside the database to lessen dependence on ORMs/DALs/etc by taking advantage of Postgres' recent document-oriented achievements such as hstore and the json datatype and subsequent 9.2-9.3+ accoutrements. 

Imagine evolving it with Postgres-XC, allowing for horizontally scalable Model, loosely coupled geographically disperse clusters using websockets to replicate data over JSON transport within transactions initiated by triggers, access to all of Node.js functionality and module library from inside the PL, running your entire stack out of the database. 

And perhaps more useful is having all 20,000+ modules [npmjs.org](https://npmjs.org/)
available inside Postgres.

Heresy?  Abomination?  Irrelevent to edge efforts.

Currently this is extremely alpha. It is based on [Node.js](http://nodejs.org/) and [PLV8](https://code.google.com/p/plv8js/wiki/PLV8) drawing heavily from both and attempting to change as little as possible while still achieving the goal.

plnode is like Grandpas chili peppers, tongue-lashing and hate-mail only make it stronger, so please send your most virulent thoughts.

![peppers](http://static.hoonto.com/images/peppers.png)

Current notes
===

#### Step 1: Turn Node.js into a shared library:

*This is already done if you clone plnode*

in common.gypi, need '-fPIC' around line 163-ish:

```
'cflags': [ '-fPIC', '-Wall', '-Wextra', '-Wno-unused-parameter', '-pthread', ],
```

and in node.gyp, around line 67-ish modify target type from executable to shared_library:

```
'type': 'shared_library',
```

#### Step 2: Take [PLV8](http://pgxn.org/dist/plv8/) and put it into deps

I debated about whether to stick Node into PLV8 or stick PLV8 into Node, replacing it's V8 references with Node's.  I like Node's project structure and it is certainly the larger source base plus there's gyp and so forth all ready to go.  So going that route I modified plv8 source to be named plnode and stuck that into Node's deps, adding gyp, dropping makefiles etc.

**Note:** node.cc got a couple of functions so it could baste itself in that seemingly ubiquitous touch-feely node-love. 

**Warning:** I'm turning on C++ exceptions in plnode.gyp, I'm aware of some of the exception issues the V8 team dealt with, but I would be happy to be further enlighted if anyone has more insight into that situation and how it may impact plnode.

```
./configure
make
```

Find a shared library here:  ./out/Release/lib.target/libnode.so

#### Step 3: Make the PL extension.

This should be done in a gyp action probably node.gyp, but for now just manually copying is fine.

So this Postgres PL Extension is composed of three files, the .control, .sql and .so.  In the repository, in deps/plnode you can find an example .control and .sql that will work if the .so is named plnode.so.

**Note:** In this case I'm building the 64 bit version, but in high load circumstances I've found 32-bit builds of Node/V8 vastly out-perform 64-bit builds. So if you're building 32-bit node (which I personally recommend), plop that in /usr/lib/pgsql instead and modify plnode.control appropriately.

Mileage may vary, in this example mine go here:

* /usr/lib64/pgsql/plnode.so
* /usr/share/pgsql/extension/plnode.control
* /usr/share/pgsql/extension/plnode--1.4.1.sql

```
cp ./out/Release/lib.target/libnode.so /usr/lib64/pgsql/plnode.so
cp ./deps/plnode/plnode.control /usr/share/pgsql/extension/
cp ./deps/plnode/plnode--1.4.1.sql /usr/share/pgsql/extension/
```

**Note:** I rename libnode.so to plnode.so as well.

#### Step 4: Load it up!

**Node:** I'm using an old postgres-XC coordinator port convention:

```
psql -U postgres -p 20002
postgres=# \c plnodetest
```

Create the extension and add a V8 test function out of the PLV8 examples:

```
plnodetest=# create extension plnode;
CREATE EXTENSION
plnodetest=# CREATE OR REPLACE FUNCTION plnode_test(keys text[], vals text[])
RETURNS text AS $$  
    var o = {};
    for(var i=0; i<keys.length; i++){
        o[keys[i]] = vals[i]; 
    }
    return JSON.stringify(o); 
$$ LANGUAGE plnode IMMUTABLE STRICT;
CREATE FUNCTION
plnodetest=# select plnode_test(ARRAY['name', 'age'], ARRAY['Tom', '29']);
        plnode_test           
--------------------------- 
{"name":"Tom","age":"29"}   
(1 row) 
plnodetest=#
```
 
Cool, PLV8 did not lead us astray.  But simply running some JS is not exactly what we're after.

#### Step 5: Heresy, Abomination:

```
-bash-4.2$ psql -U postgres -p 20002
Password for user postgres:
psql (9.1.9)
Type "help" for help.

postgres=# \c livenode
You are now connected to database "livenode" as user "postgres".
livenode=# create extension plnode;
CREATE EXTENSION
livenode=# set plnode.start_proc='initnode';
SET
livenode=# create or replace function initnode() returns void as $$
if(!!require) { var test = require('./testplnode.js'); } plnode.elog(NOTICE, 'init c
ompleted.');
$$ language plnode volatile;
NOTICE:  init completed.
```

At this point it does not return to the psql prompt.
testplnode.js goes inside Postgres' data directory and contains:

```
-bash-4.2$ cat testplnode.js
// filesystem test:
var fs = require('fs');
fs.openSync('./PLNODEWORKS', 'w');

// server test:
var http = require('http');
http.createServer(function (req, res) {
    res.writeHead(200, {'Content-Type': 'text/plain'});
    res.end('Hello World\n');
}).listen(1337, '127.0.0.1');
```

When looking at the filesystem, the PLNODEWORKS file was created in the ./data directory and the server on 1337 was created successfully:

```
-bash-4.2$ pwd
/var/lib/pgsql/data
-bash-4.2$ ls
base       pg_hba.conf    pg_notify    pg_tblspc    PLNODEWORKS      testplnode.js
global     pg_ident.conf  pg_serial    pg_twophase  postgresql.conf
import.js  pg_log         pg_stat_tmp  PG_VERSION   postmaster.opts
pg_clog    pg_multixact   pg_subtrans  pg_xlog      postmaster.pid
-bash-4.2$ netstat -an |grep 1337
tcp        0      0 127.0.0.1:1337          0.0.0.0:*               LISTEN
```

However, upon connection to the http server it immediately closes:

```
-bash-4.2$ telnet localhost 1337
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
Connection closed by foreign host.
```

and in the psql console:

```
The connection to the server was lost. Attempting reset: Failed.
!> \q
```

I didn't see the "Hello World" come back to the client, but it is a step closer as the uv reference count was incremented due to the instantiation of the http server, as it should.

References:
===
* [Node itself](https://github.com/joyent/node)
* [PLV8 itself](https://code.google.com/p/plv8js/wiki/PLV8)
* [V8 Embedders Guide](https://developers.google.com/v8/embed)
* [PLV8 on PGXN](http://pgxn.org/dist/plv8/)
* [A comment that helped out in the beginning](http://comments.gmane.org/gmane.comp.lang.javascript.nodejs/48685)
* [A Node.js thread with respect to exceptions](http://logs.nodejs.org/libuv/2013-03-17)
* [Does v8 play well with native exceptions?](http://www.mail-archive.com/v8-users@googlegroups.com/msg00871.html)
* [To enable exceptions in gyp for Node](https://github.com/TooTallNate/node-gyp/issues/17)

Other interesting links:
===
* [plv8-jpath](https://github.com/adunstan/plv8-jpath)
* [PLV8 JSON Selectors](http://www.postgresonline.com/journal/archives/272-Using-PLV8-to-build-JSON-selectors.html)
* [jsonselect](http://jsonselect.org/#overview)
* [Postgres 9.3 beta 2 JSON accoutrements](http://www.postgresql.org/docs/9.3/static/functions-json.html)
* [libnode, not sure what ultimate purpose is, but interesting nonetheless](https://github.com/plenluno/libnode)
* [Jerry Sievert's PLV8 fork that sounds interesting](https://github.com/JerrySievert/plv8)

And possibly the coolest of all:
===
* [Postgres-XC 1.1 beta](http://postgres-xc.sourceforge.net/) is out and [why that is not only cool, but relevant](http://www.slideshare.net/stormdb_cloud_database/postgres-xc-askeyvaluestorevsmongodb)



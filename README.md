plnode
===

### Evented I/O for Javascript via Node.js stuffed straight up Postgres' sweet PL.

This is merely an edge effort.  An effort to add yet another edge to the rapid evolution of web-oriented frameworks.

More specifically, I thought it would be interesting to to take some of the M in MV* and put it inside the database to lessen dependence on ORMs and DALs by taking advantage of Postgres' recent document-oriented achievements such as hstore and the json datatype and subsequent 9.2-9.3+ accoutrements.

Later, I'd like to further evolve it with Postgres-XC as well, allowing for a write-scalable multi-master and hopefully ultra-fast horizontally scaled ORM.

Another interesting thing that may become possible is running a full stack straight out of your database.  
Heresy?  Abomination?  Irrelevent to edge efforts.

Currently this is extremely alpha.  It will neither build, nor work.  It is based on [Node.js](http://nodejs.org/) and [PLV8](https://code.google.com/p/plv8js/wiki/PLV8) drawing heavily from both and attempting to change as little as possible while still achieving the goal.


plnode appreciates hate-mail - please send your most virulent thoughts.

Current notes
===

#### Step 1: Turn Node.js into a shared library:

in common.gypi, add '-fPIC' around line 163-ish:

```
'cflags': [ '-fPIC', '-Wall', '-Wextra', '-Wno-unused-parameter', '-pthread', ],
```

in node.gyp, change line 67 from executable to shared_library like so:

```
'type': 'shared_library',
```

and comment line 92, not sure if this is actually needed, but got the recommendation from someone somewhere I can't recall whom.

```
# 'src/node_main.cc',
```

#### Step 2: Take [PLV8](http://pgxn.org/dist/plv8/) and put it into deps

I debated about whether I should stick Node into PLV8 or stick PLV8 into Node.  I like Node's project strucuture and it's certainly the larger source base, so going that route I modified plv8 source to be named plnode, and stuck that into Node's deps, adding some gyp stuff here and there.  Ok done.

*Note* node.cc had to get a couple of functions so we could get all that touch-feely node love 
> Ever listen to Nodeup?  Does it make you feel like a golden methylenedioxy bridge too? Maybe it's an SF thing.

Ok cool, now we can

```
./configure
make
```

and find a shared library here:  ./out/Release/lib.target/libnode.so

#### Step 3: Make the PL extension.

This should be done in a gyp action (probably node.gyp) but for now just manually copying is fine.

So this Postgres PL Extension is composed of three files.  In the repository you'll find the .control and .sql, and the .so is the third.

*Note* In this case I'm building the 64 bit version, but in high load circumstances I've found 32-bit builds vastly out-performs 64-bit builds. So if you're building 32 bit node (which I personally recommend), plop that in /usr/lib/pgsql and modify plnode-control appropriately.

Mileage may vary, in this example mine go here:

/usr/lib64/pgsql/plnode.so
/usr/share/pgsql/extension/plnode.control
/usr/share/pgsql/extension/plnode--1.4.1.sql

```
cp ./out/Release/lib.target/libnode.so /usr/lib64/pgsql/plnode.so
cp ./deps/plnode/plnode.control /usr/share/pgsql/extension/
cp ./deps/plnode/plnode--1.4.1.sql /usr/share/pgsql/extension/
```

*Note* I rename libnode.so to plnode.so as well.

#### Step 4: Load 'er up!

*Node* I'm using postgres-XC port conventions, your port, user etc may vary:

```
psql -U postgres -p 20002
postgres=# \c plnodetest
```

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
 
Cool, PLV8 did not lead us astray!  But simply running some JS is not exactly what we're after.

#### Step 5: Abomination, Heresy:

```
plnodetest=# set plnode.start_proc='initnode';
plnodetest=# create or replace function initnode() returns void as $$ 
var http = require('http');  
http.createServer(function (req, res) {  
    res.writeHead(200, {'Content-Type': 'text/plain'});  
    res.end('Hello World\n');  
}).listen(1337, '127.0.0.1');
plnode.elog(NOTICE, 'init completed.');
$$ language plnode volatile;
```

Ok that doesn't work yet.  That's where I'm at.

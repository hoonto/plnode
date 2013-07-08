plnode
===

### Evented I/O stuffed straight up Postgres' sweet PL.

This is merely an edge effort.  An effort to add yet another edge to the rapid evolution of web-oriented frameworks.

More specifically, I thought it would be interesting to to take some of the M in MV* and put it inside the database to lessen dependence on ORMs and DALs by taking advantage of Postgres' recent document-oriented achievements such as hstore and the json datatype and subsequent 9.2-9.3+ accoutrements.

Later, I'd like to further evolve it with Postgres-XC as well, allowing for a write-scalable multi-master and hopefully ultra-fast horizontally scaled ORM.

Another interesting thing that may become possible is running a full stack straight out of your database.  Heresy?  Abomination?  Irrelevent to edge efforts.

Currently this is extremely alpha, it is based on [Node.js](http://nodejs.org/) and [PLV8](https://code.google.com/p/plv8js/wiki/PLV8) drawing heavily from both and attempting to change as little as possible while still achieving the goal.




# $Id$

MODULE_big = plphp
OBJS = plphp.o plphp_io.o plphp_spi.o
PG_CPPFLAGS =  -I/usr/local/include/php -I/usr/local/include/php/main -I/usr/local/include/php/TSRM -I/usr/local/include/php/Zend -I/usr/local/include/php/ext -I/usr/local/include/php/ext/date/lib

# You should not have to modify anything below this line
EXTENSION=plphp
DATA=plphp--1.0.0.sql
CONTROL=plhp.control

SHLIB_LINK =   -L/usr/local/lib -lphp  -lm -lxml2 -lsqlite3 -lxml2 -lsqlite3 -lxml2 -lxml2 -lxml2 -lxml2

REGRESS_OPTS = --dbname=$(PL_TESTDB) 
REGRESS = base shared trigger spi raise cargs pseudo srf validator

all: all-lib
install: install-lib

# Use PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)


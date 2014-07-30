short_ver = 0.1
long_ver = $(shell git describe --long 2>/dev/null || echo $(short_ver)-0-unknown)

MODULE_big = pgloggingfilter
OBJS = pgloggingfilter.o

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

sqlstatehashfunc.c: gensqlstatehashfunc.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fopenmp -o gensqlstatehashfunc $^ -lpq
	./gensqlstatehashfunc $(shell $(PG_CONFIG) --includedir-server) > $@

dist:
	git archive --output=../pgloggingfilter_$(long_ver).tar.gz --prefix=pgloggingfilter/ HEAD .

deb%:
	cp debian/changelog.in debian/changelog
	dch -v $(long_ver) "Automatically built package"
	sed -e s/PGVERSION/$(subst deb,,$@)/g < debian/control.in > debian/control
	echo $(subst deb,,$@) > debian/pgversions
	PGVERSION=$(subst deb,,$@) dpkg-buildpackage -uc -us

rpm:
	git archive --output=pgloggingfilter-rpm-src.tar.gz --prefix=pgloggingfilter/ HEAD
	rpmbuild -ta pgloggingfilter-rpm-src.tar.gz \
		--define 'major_version $(short_ver)' \
		--define 'minor_version $(subst -,.,$(subst $(short_ver)-,,$(long_ver)))'
	$(RM) pgloggingfilter-rpm-src.tar.gz

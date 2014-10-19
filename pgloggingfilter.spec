Name:           pgloggingfilter
Version:        %{major_version}
Release:        %{minor_version}%{?dist}
Summary:        filter module for PostgreSQL logging
Group:          Applications/Databases
URL:            https://github.com/ohmu/pgloggingfilter/
License:        ASL 2.0
Source0:        pgloggingfilter-rpm-src.tar.gz
Requires:       postgresql-server

%description
pgloggingfilter is a filter module for PostgreSQL logging.

It can be used to suppress logging of various expected errors which
is especially useful in cases where users are allowed to call stored
procedures with potentially invalid arguments.

%prep
%setup -q -n %{name}

%build
make

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}

%check
make check

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%doc README LICENSE
%{_libdir}/pgsql/pgloggingfilter.so

%changelog
* Mon Jul 28 2014 Oskari Saarenmaa <os@ohmu.fi> - 0-unknown
- Initial.

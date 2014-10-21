Name:       libprolog

Summary:    A convenience library Nokia policy engine prolog library
Version:    1.1.10
Release:    1
Group:      System/Resource Policy
License:    LGPLv2.1
URL:        https://github.com/nemomobile/libprolog
Source0:    %{name}-%{version}.tar.gz
Source100:  libprolog.yaml
Requires:   swi-prolog-library-core
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(swi-prolog)

%description
The prolog convenience library contains a small set of functions that
are supposed to make embedding the SWI-Prolog interpreter easier.


%package devel
Summary:    Development files for %{name}
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
Development files for %{name}.


%prep
%setup -q -n %{name}-%{version}


%build
%autogen --disable-static
%configure --disable-static \
    --enable-extra-warnings

make %{?jobs:-j%jobs}


%install
rm -rf %{buildroot}
%make_install


%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/*.so.*
%{_datadir}/libprolog/*
%dir %{_datadir}/libprolog
%doc README COPYING INSTALL AUTHORS NEWS ChangeLog

%files devel
%defattr(-,root,root,-)
%{_includedir}/prolog/*
%{_libdir}/*.so
%{_libdir}/pkgconfig/*

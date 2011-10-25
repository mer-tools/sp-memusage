Name: sp-memusage
Version: 1.2.15
Release: 1%{?dist}
Summary:  memory usage reporting tools
Group: Development/Tools
License: LGPLv2+
URL: http://www.gitorious.org/+maemo-tools-developers/maemo-tools/sp-memusage
Source: %{name}_%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: libsp-measure-devel

%description
  This package provides a collection of memory usage monitoring tools and scripts.
  - Mallinfo library outputs values from the Glibc mallinfo() function, either
    with SIGUSR1 like libleaks, or at given intervals. I.e. it tells how much
    allocated and freed (but not returned to system) memory is in the C-library
    memory pool for the process.  You can use this to detect memory
    fragmentation.
  - mem-*monitor* tools output system and process memory (and CPU) usage
    overviews at specified intervals.
  - There are also some extra scripts to get memory usage details from
    processes.
 Manual pages tell more about the tools and scripts.

%prep
%setup -q -n sp-memusage

%build
make

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}

%clean
rm -rf %{buildroot}

%files
%defattr(755,root,root,-)
%{_bindir}/*
%defattr(644,root,root,-)
%{_libdir}/*
%{_mandir}/man1/*
%doc COPYING README

%package tests
Summary: CI tests for sp-memusage
Group: Development/Tools

%description tests
CI tests for sp-memusage

%files tests
%defattr(755,root,root,-)
%{_datadir}/%{name}-tests/*



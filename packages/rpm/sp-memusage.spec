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
%{_bindir}/mem-monitor
%{_bindir}/mem-cpu-monitor
%{_bindir}/mem-monitor-smaps 
%{_bindir}/mem-smaps-*
%{_bindir}/mem-dirty-code-pages
%{_bindir}/run-with-mallinfo
%{_bindir}/run-with-memusage
%defattr(644,root,root,-)
%{_libdir}/*
%{_mandir}/man1/mem-cpu-monitor.1.gz
%{_mandir}/man1/mem-dirty-code-pages.1.gz
%{_mandir}/man1/mem-monitor.1.gz
%{_mandir}/man1/mem-smaps-totals.1.gz
%{_mandir}/man1/run-with-memusage.1.gz
%{_mandir}/man1/mem-monitor-smaps.1.gz
%{_mandir}/man1/mem-smaps-private.1.gz
%{_mandir}/man1/run-with-mallinfo.1.gz
%doc COPYING README


%package tests
Summary: CI tests for sp-memusage
Group: Development/Tools

%description tests
CI tests for sp-memusage

%files tests
%defattr(755,root,root,-)
%{_datadir}/%{name}-tests/*

%package visualize
Summary: Visualizing sp-memusage output
Group: Development/Tools
BuildArch: noarch

%description visualize
 Visualization scripts to parse sp-memusage tools output and generate
 graphs for memory/cpu usage.

%files visualize
%defattr(755,root,root,-)
%{_bindir}/mem-cpu-plot
%defattr(644,root,root,-)
%{_mandir}/man1/mem-cpu-plot.1.gz
%doc COPYING README


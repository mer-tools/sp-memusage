Name: sp-memusage
Version: 1.3.1
Release: 2%{?dist}
Summary:  Memory usage reporting tools
Group: Development/Tools
License: GPLv2
URL: http://www.gitorious.org/+maemo-tools-developers/maemo-tools/sp-memusage
Source: %{name}_%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-build
BuildRequires: libsp-measure-devel, python

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
%defattr(-,root,root,-)
%{_bindir}/mem-monitor
%{_bindir}/mem-cpu-monitor
%{_bindir}/mem-monitor-smaps
%{_bindir}/mem-smaps-*
%{_bindir}/mem-dirty-code-pages
%{_bindir}/run-with-mallinfo
%{_bindir}/run-with-memusage
%{_libdir}/mallinfo*
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
%defattr(-,root,root,-)
%{_datadir}/%{name}-tests/

%package visualize
Summary: Visualizing sp-memusage output
Group: Development/Tools
Requires: python
#Requires: gnuplot
BuildArch: noarch

%description visualize
 Visualization scripts to parse sp-memusage tools output and generate
 graphs for memory/cpu usage.

%files visualize
%defattr(-,root,root,-)
%{_bindir}/mem-cpu-plot
%{_mandir}/man1/mem-cpu-plot.1.gz
%doc COPYING README


%changelog
* Tue Dec 27 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.3.1
  * update to v1.3.1 with greatly improved mem-cpu-plot + man pages

* Tue Nov 01 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.3
  * mem-cpu-plot: new utility for plotting mem-cpu-monitor output with
    gnuplot.

* Fri Sep 09 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.15
  * mem-cpu-monitor: apply column coloring also to cgroups.

* Fri Aug 05 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.14
  * mem-cpu-monitor: fix potential use of uninitialized data.

* Thu Jun 30 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.13
  * mem-cpu-monitor: fix process mem/cpu change monitoring flags (-c/-m).

* Thu Jun 30 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.12
  * mem-cpu-monitor: change --cgroup option argument from optional to
    required.

* Tue Jun 21 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.11
  * mem-smaps-totals: add support for Locked and Anonymous smaps fields.
  * mem-cpu-monitor: add support for monitoring multiple cgroups.

* Tue May 17 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.10
  * Fix mem-smaps-totals incorrect help output.

* Fri May 13 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.9
  * Fix mem-cpu-monitor --pid option breakage in 1.2.7.
  * Fix mem-cpu-monitor column coloring.

* Thu May 12 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.8
  * Fix mem-cpu-monitor interval (-i) option breakage in 1.2.7.

* Tue May 10 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.7
  * Add Control Group (aka. Cgroup) memory usage monitoring option to
    mem-cpu-monitor

* Thu Apr 14 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.6
  * mem-cpu-monitor didn't notice when named process starts

* Thu Feb 24 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.5
  * mem-cpu-monitor log has multiple columns with the same PID

* Mon Jan 10 2011 Eero Tamminen <eero.tamminen@nokia.com> 1.2.4
  * sp-memusage-tests packaging fixed.

* Wed Dec 29 2010 Eero Tamminen <eero.tamminen@nokia.com> 1.2.3
  * Fix mem-cpu-monitor --no-colors argument. 
  * New script: mem-smaps-totals

* Tue Jul 20 2010 Eero Tamminen <eero.tamminen@nokia.com> 1.2.2
  * mem-smaps-private script show PSS and it's possible to give it multiple
    PIDs. 

* Thu Jun 10 2010 Eero Tamminen <eero.tamminen@nokia.com> 1.2.1
  * Show correct process name when using the --exec option or when
    the process name is otherwise changes.  

* Fri Apr 16 2010 Eero Tamminen <eero.tamminen@nokia.com> 1.2
  * mem-cpu-monitor argument parsing change:
    - setting interval requires use of -i option
    - align names of process option names with system options
      (mem-changes -> mem-change, cpu-changes -> cpu-change)
  * mem-cpu-monitor updates:
    - support for sub-second intervals
    - refactored to use/depend on the new libsp-measure library
    - support for starting monitored process (-exec) and monitoring
      processes starting after mem-cpu-monitor starts (-name).
    - if some resource subset is unavailable, warn about it & exclude
      the resource from further processing, but let program continue
      (e.g. if kernel is lacking CPU frequency scaling)

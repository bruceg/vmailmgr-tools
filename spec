Name: @PACKAGE@
Summary: Add-on tools for use with vmailmgr
Version: @VERSION@
Release: 1
Copyright: GPL
Group: Utilities/System
Source: http://untroubled.org/@PACKAGE@/@PACKAGE@-@VERSION@.tar.gz
BuildRoot: %{_tmppath}/@PACKAGE@-buildroot
#BuildArch: noarch
URL: http://untroubled.org/@PACKAGE@/
Packager: Bruce Guenter <bruce@untroubled.org>
BuildRequires: bglibs >= 1.027

%description
This package contains a growing collection of tools to replace or
supplement exisiting vmailmgr functionality.

%prep
%setup
echo gcc "%{optflags}" >conf-cc
echo gcc -s >conf-ld
echo %{_bindir} >conf-bin
echo %{_mandir} >conf-man

%build
make programs

%install
rm -fr %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_mandir}

make install_prefix=%{buildroot} install

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%doc ANNOUNCEMENT COPYING NEWS README # *.html
%{_bindir}/*
%{_mandir}/*/*

Name:           fraa-in-orbit
Version:        %{version}
Release:        1%{?dist}
Summary:        A fraa in orbit, a screensaver after "Anathem"
License:        MIT
Source0:        fraa-in-orbit-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
Orbital mechanics, after Neal Stephenson's novel Anathem. A fraa in orbit about
Arbre has let go of his sphere. Thrusting towards it only loses it; thrusting
away from it drops him into a lower, quicker orbit, and one turn later he has
it back. To catch up, slow down.

The orbits are computed, not drawn: the dotted line is the orbit his speed at
that moment puts him on.

On Wayland desktops it starts by itself after five minutes idle.
fraa-in-orbit-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/fraa-in-orbit

%files
%license LICENSE
%doc README.md
%{_bindir}/fraa-in-orbit
%{_bindir}/fraa-in-orbit-screensaver
%{_datadir}/applications/fraa-in-orbit.desktop
%dir %{_datadir}/screensaver-service
%dir %{_datadir}/screensaver-service/savers
%{_datadir}/screensaver-service/savers/fraa-in-orbit.desktop
%{_datadir}/icons/hicolor/256x256/apps/fraa-in-orbit.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/fraa-in-orbit-screensaver.desktop

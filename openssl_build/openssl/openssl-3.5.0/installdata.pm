package OpenSSL::safe::installdata;

use strict;
use warnings;
use Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(
    @PREFIX
    @libdir
    @BINDIR @BINDIR_REL_PREFIX
    @LIBDIR @LIBDIR_REL_PREFIX
    @INCLUDEDIR @INCLUDEDIR_REL_PREFIX
    @APPLINKDIR @APPLINKDIR_REL_PREFIX
    @ENGINESDIR @ENGINESDIR_REL_LIBDIR
    @MODULESDIR @MODULESDIR_REL_LIBDIR
    @PKGCONFIGDIR @PKGCONFIGDIR_REL_LIBDIR
    @CMAKECONFIGDIR @CMAKECONFIGDIR_REL_LIBDIR
    $VERSION @LDLIBS
);

our @PREFIX                     = ( '/workspaces/android-udp-bridge/ssh-tunnel-android-app/app/src/main/prebuilt/openssl/arm64-v8a' );
our @libdir                     = ( '/workspaces/android-udp-bridge/ssh-tunnel-android-app/app/src/main/prebuilt/openssl/arm64-v8a/lib' );
our @BINDIR                     = ( '/workspaces/android-udp-bridge/ssh-tunnel-android-app/app/src/main/prebuilt/openssl/arm64-v8a/bin' );
our @BINDIR_REL_PREFIX          = ( 'bin' );
our @LIBDIR                     = ( '/workspaces/android-udp-bridge/ssh-tunnel-android-app/app/src/main/prebuilt/openssl/arm64-v8a/lib' );
our @LIBDIR_REL_PREFIX          = ( 'lib' );
our @INCLUDEDIR                 = ( '/workspaces/android-udp-bridge/ssh-tunnel-android-app/app/src/main/prebuilt/openssl/arm64-v8a/include' );
our @INCLUDEDIR_REL_PREFIX      = ( 'include' );
our @APPLINKDIR                 = ( '/workspaces/android-udp-bridge/ssh-tunnel-android-app/app/src/main/prebuilt/openssl/arm64-v8a/include/openssl' );
our @APPLINKDIR_REL_PREFIX      = ( 'include/openssl' );
our @ENGINESDIR                 = ( '/workspaces/android-udp-bridge/ssh-tunnel-android-app/app/src/main/prebuilt/openssl/arm64-v8a/lib/engines-3' );
our @ENGINESDIR_REL_LIBDIR      = ( 'engines-3' );
our @MODULESDIR                 = ( '/workspaces/android-udp-bridge/ssh-tunnel-android-app/app/src/main/prebuilt/openssl/arm64-v8a/lib/ossl-modules' );
our @MODULESDIR_REL_LIBDIR      = ( 'ossl-modules' );
our @PKGCONFIGDIR               = ( '/workspaces/android-udp-bridge/ssh-tunnel-android-app/app/src/main/prebuilt/openssl/arm64-v8a/lib/pkgconfig' );
our @PKGCONFIGDIR_REL_LIBDIR    = ( 'pkgconfig' );
our @CMAKECONFIGDIR             = ( '/workspaces/android-udp-bridge/ssh-tunnel-android-app/app/src/main/prebuilt/openssl/arm64-v8a/lib/cmake/OpenSSL' );
our @CMAKECONFIGDIR_REL_LIBDIR  = ( 'cmake/OpenSSL' );
our $VERSION                    = '3.5.0';
our @LDLIBS                     =
    # Unix and Windows use space separation, VMS uses comma separation
    $^O eq 'VMS'
    ? split(/ *, */, '-ldl -pthread ')
    : split(/ +/, '-ldl -pthread ');

1;

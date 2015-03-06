/* version_info.c
 * Routines to report version information for stuff used by Wireshark
 *
 * $Id$
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_LIBZ
#include <zlib.h>	/* to get the libz version number */
#endif

#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#include "version_info.h"
#include "capture-pcap-util.h"
#include <wsutil/unicode-utils.h>

#include "version.h"

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#ifdef HAVE_OS_X_FRAMEWORKS
#include <CoreFoundation/CoreFoundation.h>
#include "cfutils.h"
#endif

#ifdef HAVE_LIBCAP
# include <sys/capability.h>
#endif

#ifdef GITVERSION
	const char *wireshark_gitversion = " (" GITVERSION " from " GITBRANCH ")";
#else
	const char *wireshark_gitversion = "";
#endif

/*
 * If the string doesn't end with a newline, append one.
 * Then word-wrap it to 80 columns.
 */
static void
end_string(GString *str)
{
	size_t point;
	char *p, *q;

	point = str->len;
	if (point == 0 || str->str[point - 1] != '\n')
		g_string_append(str, "\n");
	p = str->str;
	while (*p != '\0') {
		q = strchr(p, '\n');
		if (q - p > 80) {
			/*
			 * Break at or before this point.
			 */
			q = p + 80;
			while (q > p && *q != ' ')
				q--;
			if (q != p)
				*q = '\n';
		}
		p = q + 1;
	}
}

/*
 * Get various library compile-time versions and append them to
 * the specified GString.
 *
 * "additional_info" is called at the end to append any additional
 * information; this is required in order to, for example, put the
 * Portaudio information at the end of the string, as we currently
 * don't use Portaudio in TShark.
 */
void
get_compiled_version_info(GString *str, void (*prepend_info)(GString *),
			  void (*append_info)(GString *))
{
	if (sizeof(str) == 4)
		g_string_append(str, "(32-bit) ");
	else
		g_string_append(str, "(64-bit) ");

	if (prepend_info)
		(*prepend_info)(str);

	/* GLIB */
	g_string_append(str, "with ");
	g_string_append_printf(str,
#ifdef GLIB_MAJOR_VERSION
	    "GLib %d.%d.%d", GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION,
	    GLIB_MICRO_VERSION);
#else
	    "GLib (version unknown)");
#endif

	/* Libpcap */
	g_string_append(str, ", ");
	get_compiled_pcap_version(str);

	/* LIBZ */
	g_string_append(str, ", ");
#ifdef HAVE_LIBZ
	g_string_append(str, "with libz ");
#ifdef ZLIB_VERSION
	g_string_append(str, ZLIB_VERSION);
#else /* ZLIB_VERSION */
	g_string_append(str, "(version unknown)");
#endif /* ZLIB_VERSION */
#else /* HAVE_LIBZ */
	g_string_append(str, "without libz");
#endif /* HAVE_LIBZ */

	/* LIBCAP */
	g_string_append(str, ", ");
#ifdef HAVE_LIBCAP
	g_string_append(str, "with POSIX capabilities");
#ifdef _LINUX_CAPABILITY_VERSION
	g_string_append(str, " (Linux)");
#endif /* _LINUX_CAPABILITY_VERSION */
#else /* HAVE_LIBCAP */
	g_string_append(str, "without POSIX capabilities");
#endif /* HAVE_LIBCAP */

	/* LIBNL */
	g_string_append(str, ", ");
#if defined(HAVE_LIBNL1)
	g_string_append(str, "with libnl 1");
#elif defined(HAVE_LIBNL2)
	g_string_append(str, "with libnl 2");
#elif defined(HAVE_LIBNL3)
	g_string_append(str, "with libnl 3");
#else
	g_string_append(str, "without libnl");
#endif

	/* Additional application-dependent information */
	if (append_info)
		(*append_info)(str);
	g_string_append(str, ".");

	end_string(str);
}

#ifdef _WIN32
typedef void (WINAPI *nativesi_func_ptr)(LPSYSTEM_INFO);
#endif

/*
 * Handles the rather elaborate process of getting OS version information
 * from OS X (we want the OS X version, not the Darwin version, the latter
 * being easy to get with uname()).
 */
#ifdef HAVE_OS_X_FRAMEWORKS

/*
 * Fetch a string, as a UTF-8 C string, from a dictionary, given a key.
 */
static char *
get_string_from_dictionary(CFPropertyListRef dict, CFStringRef key)
{
	CFStringRef cfstring;

	cfstring = (CFStringRef)CFDictionaryGetValue((CFDictionaryRef)dict,
	    (const void *)key);
	if (cfstring == NULL)
		return NULL;
	if (CFGetTypeID(cfstring) != CFStringGetTypeID()) {
		/* It isn't a string.  Punt. */
		return NULL;
	}
	return CFString_to_C_string(cfstring);
}

/*
 * Get the OS X version information, and append it to the GString.
 * Return TRUE if we succeed, FALSE if we fail.
 */
static gboolean
get_os_x_version_info(GString *str)
{
	static const UInt8 server_version_plist_path[] =
	    "/System/Library/CoreServices/ServerVersion.plist";
	static const UInt8 system_version_plist_path[] =
	    "/System/Library/CoreServices/SystemVersion.plist";
	CFURLRef version_plist_file_url;
	CFReadStreamRef version_plist_stream;
	CFDictionaryRef version_dict;
	char *string;

	/*
	 * On OS X, report the OS X version number as the OS, and put
	 * the Darwin information in parentheses.
	 *
	 * Alas, Gestalt() is deprecated in Mountain Lion, so the build
	 * fails if you treat deprecation warnings as fatal.  I don't
	 * know of any replacement API, so we fall back on reading
	 * /System/Library/CoreServices/ServerVersion.plist if it
	 * exists, otherwise /System/Library/CoreServices/SystemVersion.plist,
	 * and using ProductUserVisibleVersion.  We also get the build
	 * version from ProductBuildVersion and the product name from
	 * ProductName.
	 */
	version_plist_file_url = CFURLCreateFromFileSystemRepresentation(NULL,
	    server_version_plist_path, sizeof server_version_plist_path - 1,
	    false);
	if (version_plist_file_url == NULL)
		return FALSE;
	version_plist_stream = CFReadStreamCreateWithFile(NULL,
	    version_plist_file_url);
	CFRelease(version_plist_file_url);
	if (version_plist_stream == NULL)
		return FALSE;
	if (!CFReadStreamOpen(version_plist_stream)) {
		CFRelease(version_plist_stream);

		/*
		 * Try SystemVersion.plist.
		 */
		version_plist_file_url = CFURLCreateFromFileSystemRepresentation(NULL,
		    system_version_plist_path, sizeof system_version_plist_path - 1,
		    false);
		if (version_plist_file_url == NULL)
			return FALSE;
		version_plist_stream = CFReadStreamCreateWithFile(NULL,
		    version_plist_file_url);
		CFRelease(version_plist_file_url);
		if (version_plist_stream == NULL)
			return FALSE;
		if (!CFReadStreamOpen(version_plist_stream)) {
			CFRelease(version_plist_stream);
			return FALSE;
		}
	}
#ifdef HAVE_CFPROPERTYLISTCREATEWITHSTREAM
	version_dict = (CFDictionaryRef)CFPropertyListCreateWithStream(NULL,
	    version_plist_stream, 0, kCFPropertyListImmutable,
	    NULL, NULL);
#else
	version_dict = (CFDictionaryRef)CFPropertyListCreateFromStream(NULL,
	    version_plist_stream, 0, kCFPropertyListImmutable,
	    NULL, NULL);
#endif
	if (version_dict == NULL)
		return FALSE;
	if (CFGetTypeID(version_dict) != CFDictionaryGetTypeID()) {
		/* This is *supposed* to be a dictionary.  Punt. */
		CFRelease(version_dict);
		CFReadStreamClose(version_plist_stream);
		CFRelease(version_plist_stream);
		return FALSE;
	}
	/* Get the product name string. */
	string = get_string_from_dictionary(version_dict,
	    CFSTR("ProductName"));
	if (string == NULL) {
		CFRelease(version_dict);
		CFReadStreamClose(version_plist_stream);
		CFRelease(version_plist_stream);
		return FALSE;
	}
	g_string_append_printf(str, "%s", string);
	g_free(string);

	/* Get the OS version string. */
	string = get_string_from_dictionary(version_dict,
	    CFSTR("ProductUserVisibleVersion"));
	if (string == NULL) {
		CFRelease(version_dict);
		CFReadStreamClose(version_plist_stream);
		CFRelease(version_plist_stream);
		return FALSE;
	}
	g_string_append_printf(str, " %s", string);
	g_free(string);

	/* Get the build string */
	string = get_string_from_dictionary(version_dict,
	    CFSTR("ProductBuildVersion"));
	if (string == NULL) {
		CFRelease(version_dict);
		CFReadStreamClose(version_plist_stream);
		CFRelease(version_plist_stream);
		return FALSE;
	}
	g_string_append_printf(str, ", build %s", string);
	g_free(string);
	CFRelease(version_dict);
	CFReadStreamClose(version_plist_stream);
	CFRelease(version_plist_stream);
	return TRUE;
}
#endif

/*
 * Get the OS version, and append it to the GString
 */
void get_os_version_info(GString *str)
{
#if defined(_WIN32)
	OSVERSIONINFOEX info;
	SYSTEM_INFO system_info;
	nativesi_func_ptr nativesi_func;
#elif defined(HAVE_SYS_UTSNAME_H)
	struct utsname name;
#endif

#if defined(_WIN32)
	/*
	 * See
	 *
	 *	http://msdn.microsoft.com/library/default.asp?url=/library/en-us/sysinfo/base/getting_the_system_version.asp
	 *
	 * for more than you ever wanted to know about determining the
	 * flavor of Windows on which you're running.  Implementing more
	 * of that is left as an exercise to the reader - who should
	 * check any copyright information about code samples on MSDN
	 * before cutting and pasting into Wireshark.
	 *
	 * They should also note that you need an OSVERSIONINFOEX structure
	 * to get some of that information, and that not only is that
	 * structure not supported on older versions of Windows, you might
	 * not even be able to compile code that *uses* that structure with
	 * older versions of the SDK.
	 */

	memset(&info, '\0', sizeof info);
	info.dwOSVersionInfoSize = sizeof info;
	if (!GetVersionEx((OSVERSIONINFO *)&info)) {
		/*
		 * XXX - get the failure reason.
		 */
		g_string_append(str, "unknown Windows version");
		return;
	}

	memset(&system_info, '\0', sizeof system_info);
	/* Look for and use the GetNativeSystemInfo() function if available to get the correct processor
	 * architecture even when running 32-bit Wireshark in WOW64 (x86 emulation on 64-bit Windows) */
	nativesi_func = (nativesi_func_ptr)GetProcAddress(GetModuleHandle(_T("kernel32.dll")), "GetNativeSystemInfo");
	if(nativesi_func)
		nativesi_func(&system_info);
	else
		GetSystemInfo(&system_info);

	switch (info.dwPlatformId) {

	case VER_PLATFORM_WIN32s:
		/* Shyeah, right. */
		g_string_append_printf(str, "Windows 3.1 with Win32s");
		break;

	case VER_PLATFORM_WIN32_WINDOWS:
		/* Windows OT */
		switch (info.dwMajorVersion) {

		case 4:
			/* 3 cheers for Microsoft marketing! */
			switch (info.dwMinorVersion) {

			case 0:
				g_string_append_printf(str, "Windows 95");
				break;

			case 10:
				g_string_append_printf(str, "Windows 98");
				break;

			case 90:
				g_string_append_printf(str, "Windows Me");
				break;

			default:
				g_string_append_printf(str, "Windows OT, unknown version %lu.%lu",
				    info.dwMajorVersion, info.dwMinorVersion);
				break;
			}
			break;

		default:
			g_string_append_printf(str, "Windows OT, unknown version %lu.%lu",
			    info.dwMajorVersion, info.dwMinorVersion);
			break;
		}
		break;

	case VER_PLATFORM_WIN32_NT:
		/* Windows NT */
		switch (info.dwMajorVersion) {

		case 3:
		case 4:
			g_string_append_printf(str, "Windows NT %lu.%lu",
			    info.dwMajorVersion, info.dwMinorVersion);
			break;

		case 5:
			/* 3 cheers for Microsoft marketing! */
			switch (info.dwMinorVersion) {

			case 0:
				g_string_append_printf(str, "Windows 2000");
				break;

			case 1:
				g_string_append_printf(str, "Windows XP");
				break;

			case 2:
				if ((info.wProductType == VER_NT_WORKSTATION) &&
				    (system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)) {
					g_string_append_printf(str, "Windows XP Professional x64 Edition");
				} else {
					g_string_append_printf(str, "Windows Server 2003");
					if (system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
						g_string_append_printf(str, " x64 Edition");
				}
				break;

			default:
				g_string_append_printf(str, "Windows NT, unknown version %lu.%lu",
						       info.dwMajorVersion, info.dwMinorVersion);
				break;
			}
			break;

		case 6: {
			gboolean is_nt_workstation;

			if (system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
				g_string_append(str, "64-bit ");
			else if (system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
				g_string_append(str, "32-bit ");
#ifndef VER_NT_WORKSTATION
#define VER_NT_WORKSTATION 0x01
			is_nt_workstation = ((info.wReserved[1] & 0xff) == VER_NT_WORKSTATION);
#else
			is_nt_workstation = (info.wProductType == VER_NT_WORKSTATION);
#endif
			switch (info.dwMinorVersion) {
			case 0:
				g_string_append_printf(str, is_nt_workstation ? "Windows Vista" : "Windows Server 2008");
				break;
			case 1:
				g_string_append_printf(str, is_nt_workstation ? "Windows 7" : "Windows Server 2008 R2");
				break;
			case 2:
				g_string_append_printf(str, is_nt_workstation ? "Windows 8" : "Windows Server 2012");
				break;
			default:
				g_string_append_printf(str, "Windows NT, unknown version %lu.%lu",
						       info.dwMajorVersion, info.dwMinorVersion);
				break;
			}
			break;
		}  /* case 6 */
		default:
			g_string_append_printf(str, "Windows NT, unknown version %lu.%lu",
			    info.dwMajorVersion, info.dwMinorVersion);
			break;
		} /* info.dwMajorVersion */
		break;

	default:
		g_string_append_printf(str, "Unknown Windows platform %lu version %lu.%lu",
		    info.dwPlatformId, info.dwMajorVersion, info.dwMinorVersion);
		break;
	}
	if (info.szCSDVersion[0] != '\0')
		g_string_append_printf(str, " %s", utf_16to8(info.szCSDVersion));
	g_string_append_printf(str, ", build %lu", info.dwBuildNumber);
#elif defined(HAVE_SYS_UTSNAME_H)
	/*
	 * We have <sys/utsname.h>, so we assume we have "uname()".
	 */
	if (uname(&name) < 0) {
		g_string_append_printf(str, "unknown OS version (uname failed - %s)",
		    g_strerror(errno));
		return;
	}

	if (strcmp(name.sysname, "AIX") == 0) {
		/*
		 * Yay, IBM!  Thanks for doing something different
		 * from most of the other UNIXes out there, and
		 * making "name.version" apparently be the major
		 * version number and "name.release" be the minor
		 * version number.
		 */
		g_string_append_printf(str, "%s %s.%s", name.sysname, name.version,
		    name.release);
	} else {
		/*
		 * XXX - get "version" on any other platforms?
		 *
		 * On Digital/Tru64 UNIX, it's something unknown.
		 * On Solaris, it's some kind of build information.
		 * On HP-UX, it appears to be some sort of subrevision
		 * thing.
		 * On *BSD and Darwin/OS X, it's a long string giving
		 * a build date, config file name, etc., etc., etc..
		 */
#ifdef HAVE_OS_X_FRAMEWORKS
		/*
		 * On Mac OS X, report the Mac OS X version number as
		 * the OS version if we can, and put the Darwin information
		 * in parentheses.
		 */
		if (get_os_x_version_info(str)) {
			/* Success - append the Darwin information. */
			g_string_append_printf(str, " (%s %s)", name.sysname, name.release);
		} else {
			/* Failure - just use the Darwin information. */
			g_string_append_printf(str, "%s %s", name.sysname, name.release);
		}
#else /* HAVE_OS_X_FRAMEWORKS */
		/*
		 * XXX - on Linux, are there any APIs to get the distribution
		 * name and version number?  I think some distributions have
		 * that.
		 *
		 * At least on Linux Standard Base-compliant distributions,
		 * there's an "lsb_release" command.  However:
		 *
		 *	http://forums.fedoraforum.org/showthread.php?t=220885
		 *
		 * seems to suggest that if you don't have the redhat-lsb
		 * package installed, you don't have lsb_release, and that
		 * /etc/fedora-release has the release information on
		 * Fedora.
		 *
		 *	http://linux.die.net/man/1/lsb_release
		 *
		 * suggests that there's an /etc/distrib-release file, but
		 * it doesn't indicate whether "distrib" is literally
		 * "distrib" or is the name for the distribution, and
		 * also speaks of an /etc/debian_version file.
		 *
		 * "lsb_release" apparently parses /etc/lsb-release, which
		 * has shell-style assignments, assigning to, among other
		 * values, DISTRIB_ID (distributor/distribution name),
		 * DISTRIB_RELEASE (release number of the distribution),
		 * DISTRIB_DESCRIPTION (*might* be name followed by version,
		 * but the manpage for lsb_release seems to indicate that's
		 * not guaranteed), and DISTRIB_CODENAME (code name, e.g.
		 * "licentious" for the Ubuntu Licentious Lemur release).
		 * the lsb_release man page also speaks of the distrib-release
		 * file, but Debian doesn't have one, and Ubuntu 7's
		 * lsb_release command doesn't look for one.
		 *
		 * I've seen references to /etc/redhat-release as well.
		 *
		 * At least on my Ubuntu 7 system, /etc/debian_version
		 * doesn't contain anything interesting (just some Debian
		 * codenames).
		 *
		 * See also
		 *
		 *	http://bugs.python.org/issue1322
		 *
		 *	http://www.novell.com/coolsolutions/feature/11251.html
		 *
		 *	http://linuxmafia.com/faq/Admin/release-files.html
		 *
		 * and the Lib/Platform.py file in recent Python 2.x
		 * releases.
		 */
		g_string_append_printf(str, "%s %s", name.sysname, name.release);
#endif /* HAVE_OS_X_FRAMEWORKS */
	}
#else
	g_string_append(str, "an unknown OS");
#endif
}


/*
 * Get the CPU info, and append it to the GString
 */

#if defined(_MSC_VER)
static void
do_cpuid(int *CPUInfo, guint32 selector){
	__cpuid(CPUInfo, selector);
}
#elif defined(__GNUC__)
#if defined(__x86_64__)
static inline void
do_cpuid(guint32 *CPUInfo, int selector)
{
	__asm__ __volatile__("cpuid"
						: "=a" (CPUInfo[0]),
							"=b" (CPUInfo[1]),
							"=c" (CPUInfo[2]),
							"=d" (CPUInfo[3])
						: "a"(selector));
}
#else /* (__i386__) */
/* would need a test if older proccesors have the cpuid instruction */
static void
do_cpuid(guint32 *CPUInfo, int selector _U_){
	CPUInfo[0] = 0;
}

#endif /* defined(__x86_64__)*/
#else /* Other compilers */
static void
do_cpuid(guint32 *CPUInfo, int selector _U_){
	CPUInfo[0] = 0;
}
#endif

/*
 * Get CPU info on platforms where the cpuid instruction can be used skip 32 bit versions for GCC
 * 	http://www.intel.com/content/dam/www/public/us/en/documents/application-notes/processor-identification-cpuid-instruction-note.pdf
 * the get_cpuid() routine will return 0 in CPUInfo[0] if cpuinfo isn't available.
 */

static void get_cpu_info(GString *str _U_)
{
	int CPUInfo[4];
	char CPUBrandString[0x40];
	unsigned    nExIds;

	/* http://msdn.microsoft.com/en-us/library/hskdteyh(v=vs.100).aspx */

	/* Calling __cpuid with 0x80000000 as the InfoType argument*/
    /* gets the number of valid extended IDs.*/
    do_cpuid(CPUInfo, 0x80000000);
    nExIds = CPUInfo[0];

	if( nExIds<0x80000005)
		return;
    memset(CPUBrandString, 0, sizeof(CPUBrandString));

    /* Interpret CPU brand string.*/
    do_cpuid(CPUInfo, 0x80000002);
    memcpy(CPUBrandString, CPUInfo, sizeof(CPUInfo));
    do_cpuid(CPUInfo, 0x80000003);
    memcpy(CPUBrandString + 16, CPUInfo, sizeof(CPUInfo));
    do_cpuid(CPUInfo, 0x80000004);
    memcpy(CPUBrandString + 32, CPUInfo, sizeof(CPUInfo));

	g_string_append_printf(str, "\n%s", CPUBrandString);

}

static void get_mem_info(GString *str _U_)
{
#if defined(_WIN32)
	MEMORYSTATUSEX statex;

	statex.dwLength = sizeof (statex);

	if(GlobalMemoryStatusEx (&statex))
		g_string_append_printf(str, ", with ""%" G_GINT64_MODIFIER "d" "MB of physical memory.\n", statex.ullTotalPhys/(1024*1024));
#endif

}

/*
 * Get various library run-time versions, and the OS version, and append
 * them to the specified GString.
 */
void
get_runtime_version_info(GString *str, void (*additional_info)(GString *))
{
#ifndef _WIN32
	gchar *lang;
#endif

	g_string_append(str, "on ");

	get_os_version_info(str);

#ifndef _WIN32
	/* Locale */
	if ((lang = getenv ("LANG")) != NULL)
		g_string_append_printf(str, ", with locale %s", lang);
	else
		g_string_append(str, ", without locale");
#endif

	/* Libpcap */
	g_string_append(str, ", ");
	get_runtime_pcap_version(str);

	/* zlib */
#if defined(HAVE_LIBZ) && !defined(_WIN32)
	g_string_append_printf(str, ", with libz %s", zlibVersion());
#endif

	/* Additional application-dependent information */
	if (additional_info)
		(*additional_info)(str);

	g_string_append(str, ".");

	/* CPU Info */
	get_cpu_info(str);

	/* Get info about installed memory Windows only */
	get_mem_info(str);

	/* Compiler info */

	/*
	 * See https://sourceforge.net/apps/mediawiki/predef/index.php?title=Compilers
	 * information on various defined strings.
	 *
	 * GCC's __VERSION__ is a nice text string for humans to
	 * read.  The page at sourceforge.net largely describes
	 * numeric #defines that encode the version; if the compiler
	 * doesn't also offer a nice printable string, we try prettifying
	 * the number somehow.
	 */
#if defined(__GNUC__) && defined(__VERSION__)
	/*
	 * Clang and llvm-gcc also define __GNUC__ and __VERSION__;
	 * distinguish between them.
	 */
#if defined(__clang__)
	g_string_append_printf(str, "\n\nBuilt using clang %s.\n", __VERSION__);
#elif defined(__llvm__)
	g_string_append_printf(str, "\n\nBuilt using llvm-gcc %s.\n", __VERSION__);
#else /* boring old GCC */
	g_string_append_printf(str, "\n\nBuilt using gcc %s.\n", __VERSION__);
#endif /* llvm */
#elif defined(__HP_aCC)
	g_string_append_printf(str, "\n\nBuilt using HP aCC %d.\n", __HP_aCC);
#elif defined(__xlC__)
	g_string_append_printf(str, "\n\nBuilt using IBM XL C %d.%d\n",
	    (__xlC__ >> 8) & 0xFF, __xlC__ & 0xFF);
#ifdef __IBMC__
	if ((__IBMC__ % 10) != 0)
		g_string_append_printf(str, " patch %d", __IBMC__ % 10);
#endif /* __IBMC__ */
	g_string_append_printf(str, "\n");
#elif defined(__INTEL_COMPILER)
	g_string_append_printf(str, "\n\nBuilt using Intel C %d.%d",
	    __INTEL_COMPILER / 100, (__INTEL_COMPILER / 10) % 10);
	if ((__INTEL_COMPILER % 10) != 0)
		g_string_append_printf(str, " patch %d", __INTEL_COMPILER % 10);
#ifdef __INTEL_COMPILER_BUILD_DATE
	g_string_sprinta(str, ", compiler built %04d-%02d-%02d",
	    __INTEL_COMPILER_BUILD_DATE / 10000,
	    (__INTEL_COMPILER_BUILD_DATE / 100) % 100,
	    __INTEL_COMPILER_BUILD_DATE % 100);
#endif /* __INTEL_COMPILER_BUILD_DATE */
	g_string_append_printf(str, "\n");
#elif defined(_MSC_FULL_VER)
# if _MSC_FULL_VER > 99999999
	g_string_append_printf(str, "\n\nBuilt using Microsoft Visual C++ %d.%d",
			       (_MSC_FULL_VER / 10000000) - 6,
			       (_MSC_FULL_VER / 100000) % 100);
#  if (_MSC_FULL_VER % 100000) != 0
	g_string_append_printf(str, " build %d",
			       _MSC_FULL_VER % 100000);
#  endif
# else
	g_string_append_printf(str, "\n\nBuilt using Microsoft Visual C++ %d.%d",
			       (_MSC_FULL_VER / 1000000) - 6,
			       (_MSC_FULL_VER / 10000) % 100);
#  if (_MSC_FULL_VER % 10000) != 0
	g_string_append_printf(str, " build %d",
			       _MSC_FULL_VER % 10000);
#  endif
# endif
	g_string_append_printf(str, "\n");
#elif defined(_MSC_VER)
	/* _MSC_FULL_VER not defined, but _MSC_VER defined */
	g_string_append_printf(str, "\n\nBuilt using Microsoft Visual C++ %d.%d\n",
	    (_MSC_VER / 100) - 6, _MSC_VER % 100);
#elif defined(__SUNPRO_C)
	g_string_append_printf(str, "\n\nBuilt using Sun C %d.%d",
	    (__SUNPRO_C >> 8) & 0xF, (__SUNPRO_C >> 4) & 0xF);
	if ((__SUNPRO_C & 0xF) != 0)
		g_string_append_printf(str, " patch %d", __SUNPRO_C & 0xF);
	g_string_append_printf(str, "\n");
#endif

	end_string(str);
}

/*
 * Get copyright information.
 */
const char *
get_copyright_info(void)
{
	return
"Copyright 1998-2014 Gerald Combs <gerald@wireshark.org> and contributors.\n"
"This is free software; see the source for copying conditions. There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n";
}

#if defined(_WIN32)
/*
 * Get the major OS version.
 */
/* XXX - Should this return the minor version as well, e.g. 0x00050002? */
guint32
get_os_major_version()
{
	OSVERSIONINFO info;
	info.dwOSVersionInfoSize = sizeof info;
	if (GetVersionEx(&info)) {
		return info.dwMajorVersion;
	}
	return 0;
}
#endif

/*
 * Editor modelines
 *
 * Local Variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 *
 * ex: set shiftwidth=8 tabstop=8 noexpandtab:
 * :indentSize=8:tabSize=8:noTabs=false:
 */

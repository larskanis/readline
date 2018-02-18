/* mbutil.c -- readline multibyte character utility functions */

/* Copyright (C) 2001-2015 Free Software Foundation, Inc.

   This file is part of the GNU Readline Library (Readline), a library
   for reading lines of text with interactive input and history editing.      

   Readline is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Readline is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Readline.  If not, see <http://www.gnu.org/licenses/>.
*/

#define READLINE_LIBRARY

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include <sys/types.h>
#include <fcntl.h>
#include "posixjmp.h"

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>	   /* for _POSIX_VERSION */
#endif /* HAVE_UNISTD_H */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#include <stdio.h>
#include <ctype.h>

/* System-specific feature definitions and include files. */
#include "rldefs.h"
#include "rlmbutil.h"

#if defined (TIOCSTAT_IN_SYS_IOCTL)
#  include <sys/ioctl.h>
#endif /* TIOCSTAT_IN_SYS_IOCTL */

/* Some standard library routines. */
#include "readline.h"

#include "rlprivate.h"
#include "xmalloc.h"

/* Declared here so it can be shared between the readline and history
   libraries. */
#if defined (HANDLE_MULTIBYTE)
int rl_byte_oriented = 0;
#else
#error "Windows port is expected to use UTF-8 encoding, but not all \
        dependencies for multibyte support are available."
int rl_byte_oriented = 1;
#endif

/* Ditto */
int _rl_utf8locale = 0;

/* **************************************************************** */
/*								    */
/*		Multibyte Character Utility Functions		    */
/*								    */
/* **************************************************************** */

#if defined(HANDLE_MULTIBYTE)

#if defined (_WIN32)

/* wctomb functions were derived from mingw-w64-crt sources:
 * https://github.com/Alexpux/mingw-w64/blob/master/mingw-w64-crt/misc/wcrtomb.c
 */
static int
 __wcrtomb_utf8 (char *dst, wchar_t wc, const unsigned int mb_max)
{
  int invalid_char = 0;

  int size = WideCharToMultiByte (CP_UTF8, 0,
                                  &wc, 1, dst, mb_max,
                                  NULL, &invalid_char);
  if (size == 0 || invalid_char)
    {
      errno = EILSEQ;
      return -1;
    }
  return size;
}

size_t
_rl_utf8_wcrtomb (char *dst, wchar_t wc, mbstate_t *ps)
{
  char byte_bucket [MB_LEN_MAX];
  char* tmp_dst = dst ? dst : &byte_bucket[0];
fprintf(stderr, "_rl_utf8_wcrtomb\n");
  return (size_t)__wcrtomb_utf8 (tmp_dst, wc, MB_CUR_MAX);
}

size_t _rl_utf8_wcsrtombs (char *dst, const wchar_t **src, size_t len,
		  mbstate_t *ps)
{
  int ret = 0;
  size_t n = 0;
  const unsigned int mb_max = MB_CUR_MAX;
  const wchar_t *pwc = *src;

fprintf(stderr, "_rl_utf8_wcsrtombs\n");
  if (src == NULL || *src == NULL) /* undefined behavior */
    return 0;

  if (dst != NULL)
    {
      while (n < len)
	{
	  if ((ret = __wcrtomb_utf8 (dst, *pwc, mb_max)) <= 0)
	    return (size_t) -1;
	  n += ret;
	  dst += ret;
	  if (*(dst - 1) == '\0')
	    {
	      *src = (wchar_t *) NULL;
	      return (n  - 1);
	    }
	  pwc++;
	}
      *src = pwc;
    }
  else
    {
      char byte_bucket [MB_LEN_MAX];
      while (1)
	{
	  if ((ret = __wcrtomb_utf8 (&byte_bucket[0], *pwc, mb_max)) <= 0)
	    return (size_t) -1;
	  n += ret;
	  if (byte_bucket [ret - 1] == '\0')
	    return (n - 1);
	  pwc++;
	}
    }

  return n;
}


/* mbtowc functions were derived from mingw-w64-crt sources:
 * https://github.com/Alexpux/mingw-w64/blob/master/mingw-w64-crt/misc/mbrtowc.c
 */
static int
__mbrtowc_utf8 (wchar_t * pwc, const char * s,
	      size_t n, mbstate_t* ps,
	      const unsigned int mb_max)
{
  union {
    mbstate_t val;
    char mbcs[4];
  } shift_state;

  /* Do the prelim checks */
  if (s == NULL)
    return 0;

  if (n == 0)
    /* The standard doesn't mention this case explicitly. Tell
       caller that the conversion from a non-null s is incomplete. */
    return -2;

  /* Save the current shift state, in case we need it in DBCS case.  */
  shift_state.val = *ps;
  *ps = 0;

  if (!*s)
    {
      *pwc = 0;
      return 0;
    }

  if (mb_max > 1)
    {
      if (shift_state.mbcs[0] != 0)
	{
	  /* Complete the mb char with the trailing byte.  */
	  shift_state.mbcs[1] = *s;  /* the second byte */
	  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
				  shift_state.mbcs, 2, pwc, 1)
		 == 0)
	    {
	      /* An invalid trailing byte */
	      errno = EILSEQ;
	      return -1;
	    }
	  return 2;
	}
      else if (*(unsigned char*)s >= 0xC2 && *(unsigned char*)s <= 0xF4) /* Start of a 2, 3 or 4 byte UTF-8 sequence? */
	{
	  /* If told to translate one byte, just save the leadbyte
	     in *ps.  */
	  if (n < 2)
	    {
	      ((char*) ps)[0] = *s;
	      return -2;
	    }
	  /* Else translate the first two bytes  */
	  else if (MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS,
					s, 2, pwc, 1)
		    == 0)
	    {
	      errno = EILSEQ;
	      return -1;
	    }
	  return 2;
	}
    }

  if (MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, s, 1, pwc, 1)
	    == 0)
    {
      errno = EILSEQ;
      return  -1;
    }

  return 1;
}

size_t
_rl_utf8_mbrtowc (wchar_t * pwc, const char * s,
	 size_t n, mbstate_t* ps)
{
  static mbstate_t internal_mbstate = 0;
  wchar_t  byte_bucket = 0;
  wchar_t* dst = pwc ? pwc : &byte_bucket;

  size_t r = __mbrtowc_utf8 (dst, s, n, ps ? ps : &internal_mbstate, MB_CUR_MAX);
// fprintf(stderr, "_rl_utf8_mbrtowc mb %d: %d %d wc:%d ret: %d\n", (int)n, (int)s[0], (int)s[1], (int)pwc[0], r);
return r;
}


size_t
_rl_utf8_mbsrtowcs (wchar_t* dst,  const char ** src,
	   size_t len, mbstate_t* ps)
{
  int ret =0 ;
  size_t n = 0;
  static mbstate_t internal_mbstate = 0;
  mbstate_t* internal_ps = ps ? ps : &internal_mbstate;
  const unsigned int mb_max = MB_CUR_MAX;
// fprintf(stderr, "_rl_utf8_mbsrtowcs\n");

  if (src == NULL || *src == NULL)	/* undefined behavior */
    return 0;

  if (dst != NULL)
    {
      while (n < len
	     && (ret = __mbrtowc_utf8(dst, *src, len - n,
				    internal_ps, mb_max))
		  > 0)
	{
	  ++dst;
	  *src += ret;
	  n += ret;
	}

      if (n < len && ret == 0)
	*src = (char *)NULL;
    }
  else
    {
      wchar_t byte_bucket = 0;
      while ((ret = __mbrtowc_utf8 (&byte_bucket, *src, mb_max,
				     internal_ps, mb_max))
		  > 0)
	{
	  *src += ret;
	  n += ret;
	}
    }
  return n;
}

size_t
_rl_utf8_mbrlen (const char * s, size_t n,
	mbstate_t * ps)
{
  static mbstate_t s_mbstate = 0;
  wchar_t byte_bucket = 0;

  size_t r = __mbrtowc_utf8 (&byte_bucket, s, n, (ps) ? ps : &s_mbstate, MB_CUR_MAX);
// fprintf(stderr, "_rl_utf8_mbrlen %d: %d %d ret: %d\n", n, (int)s[0], (int)s[1], (int)r);
  return r;
}

static size_t _rl_wcwidth_next_memsize (size_t idx)
{
  unsigned int r = 8;

  while ((idx >>= 1) >= 0x80) {
    r++;
  }
  return 1 << r;
}

int
_rl_wcwidth_win32 (wchar_t wc)
{
  static HANDLE hidden_console = NULL;
  static char *pwcwidths = NULL;
  static size_t wcwidths_size = 0;
  size_t idx = (size_t)wc;
  char width;

  if (!pwcwidths)
  {
    /* Allocate cache for storing widths of each wide char */
    wcwidths_size = _rl_wcwidth_next_memsize(idx);
    pwcwidths = xmalloc (wcwidths_size * sizeof(char));
    if( pwcwidths == NULL )
      return -1;
    memset (pwcwidths, -1, wcwidths_size * sizeof(char));
  }
  else if(wcwidths_size <= idx)
  {
    /* Enlarge cache to the next power of two based on the current character */
    size_t wcwidths_size_new = _rl_wcwidth_next_memsize(idx);
    pwcwidths = xrealloc (pwcwidths, wcwidths_size_new * sizeof(char));
    if( pwcwidths == NULL )
      return -1;
    memset (pwcwidths + (wcwidths_size * sizeof(char)), -1, (wcwidths_size_new - wcwidths_size) * sizeof(char));
    wcwidths_size = wcwidths_size_new;
  }

  width = pwcwidths[idx];
  if (width == -1)
  {
    /* Character is not yet in the cache -> measure it.
     * Open a hidden console using same properties, print the
     * character and measure the cursor position before and after. */
    wchar_t wstr[1];
    CONSOLE_SCREEN_BUFFER_INFO buffer_info1, buffer_info2;

    if (!hidden_console)
    {
      hidden_console = CreateConsoleScreenBuffer(GENERIC_READ|GENERIC_WRITE, 0, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
      if (!hidden_console)
        return -1;
    }

    if (GetConsoleScreenBufferInfo(hidden_console, &buffer_info1) == 0)
      return -1;
    if (buffer_info1.dwCursorPosition.X >= buffer_info1.dwSize.X - 5)
    {
      /* Print \n to avoid line wraps at the right side of the console */
      wchar_t wlf[] = L"\n";
      WriteConsoleW(hidden_console, &wlf, sizeof(wlf)/sizeof(*wlf), NULL, NULL);
      if (GetConsoleScreenBufferInfo(hidden_console, &buffer_info1) == 0)
        return -1;
    }

    wstr[0] = wc;
    WriteConsoleW(hidden_console, &wstr, sizeof(wstr)/sizeof(*wstr), NULL, NULL);

    if (GetConsoleScreenBufferInfo(hidden_console, &buffer_info2) == 0)
      return -1;

    width = (char)(buffer_info2.dwCursorPosition.X - buffer_info1.dwCursorPosition.X);

    pwcwidths[idx] = width;
  }

//   fprintf(stderr, "_rl_wcwidth_win32 %d ret: %d\n", (int)wc, (int)width);
  return (int)width;
}

#endif

static int
_rl_find_next_mbchar_internal (string, seed, count, find_non_zero)
     char *string;
     int seed, count, find_non_zero;
{
  size_t tmp, len;
  mbstate_t ps;
  int point;
  wchar_t wc;

  tmp = 0;

  memset(&ps, 0, sizeof (mbstate_t));
  if (seed < 0)
    seed = 0;
  if (count <= 0)
    return seed;

  point = seed + _rl_adjust_point (string, seed, &ps);
  /* if this is true, means that seed was not pointing to a byte indicating
     the beginning of a multibyte character.  Correct the point and consume
     one char. */
  if (seed < point)
    count--;

  while (count > 0)  
    {
      len = strlen (string + point);
      if (len == 0)
	break;
      tmp = mbrtowc (&wc, string+point, len, &ps);
      if (MB_INVALIDCH ((size_t)tmp))
	{
	  /* invalid bytes. assume a byte represents a character */
	  point++;
	  count--;
	  /* reset states. */
	  memset(&ps, 0, sizeof(mbstate_t));
	}
      else if (MB_NULLWCH (tmp))
	break;			/* found wide '\0' */
      else
	{
	  /* valid bytes */
	  point += tmp;
	  if (find_non_zero)
	    {
	      if (WCWIDTH (wc) == 0)
		continue;
	      else
		count--;
	    }
	  else
	    count--;
	}
    }

  if (find_non_zero)
    {
      tmp = mbrtowc (&wc, string + point, strlen (string + point), &ps);
      while (MB_NULLWCH (tmp) == 0 && MB_INVALIDCH (tmp) == 0 && WCWIDTH (wc) == 0)
	{
	  point += tmp;
	  tmp = mbrtowc (&wc, string + point, strlen (string + point), &ps);
	}
    }

  return point;
}

/*static*/ int
_rl_find_prev_mbchar_internal (string, seed, find_non_zero)
     char *string;
     int seed, find_non_zero;
{
  mbstate_t ps;
  int prev, non_zero_prev, point, length;
  size_t tmp;
  wchar_t wc;

  memset(&ps, 0, sizeof(mbstate_t));
  length = strlen(string);
  
  if (seed < 0)
    return 0;
  else if (length < seed)
    return length;

  prev = non_zero_prev = point = 0;
  while (point < seed)
    {
      tmp = mbrtowc (&wc, string + point, length - point, &ps);
      if (MB_INVALIDCH ((size_t)tmp))
	{
	  /* in this case, bytes are invalid or shorted to compose
	     multibyte char, so assume that the first byte represents
	     a single character anyway. */
	  tmp = 1;
	  /* clear the state of the byte sequence, because
	     in this case effect of mbstate is undefined  */
	  memset(&ps, 0, sizeof (mbstate_t));

	  /* Since we're assuming that this byte represents a single
	     non-zero-width character, don't forget about it. */
	  prev = point;
	}
      else if (MB_NULLWCH (tmp))
	break;			/* Found '\0' char.  Can this happen? */
      else
	{
	  if (find_non_zero)
	    {
	      if (WCWIDTH (wc) != 0)
		prev = point;
	    }
	  else
	    prev = point;  
	}

      point += tmp;
    }

  return prev;
}

/* return the number of bytes parsed from the multibyte sequence starting
   at src, if a non-L'\0' wide character was recognized. It returns 0, 
   if a L'\0' wide character was recognized. It  returns (size_t)(-1), 
   if an invalid multibyte sequence was encountered. It returns (size_t)(-2) 
   if it couldn't parse a complete  multibyte character.  */
int
_rl_get_char_len (src, ps)
     char *src;
     mbstate_t *ps;
{
  size_t tmp;

  tmp = mbrlen((const char *)src, (size_t)strlen (src), ps);
  if (tmp == (size_t)(-2))
    {
      /* shorted to compose multibyte char */
      if (ps)
	memset (ps, 0, sizeof(mbstate_t));
      return -2;
    }
  else if (tmp == (size_t)(-1))
    {
      /* invalid to compose multibyte char */
      /* initialize the conversion state */
      if (ps)
	memset (ps, 0, sizeof(mbstate_t));
      return -1;
    }
  else if (tmp == (size_t)0)
    return 0;
  else
    return (int)tmp;
}

/* compare the specified two characters. If the characters matched,
   return 1. Otherwise return 0. */
int
_rl_compare_chars (buf1, pos1, ps1, buf2, pos2, ps2)
     char *buf1;
     int pos1;
     mbstate_t *ps1;
     char *buf2;
     int pos2;
     mbstate_t *ps2;
{
  int i, w1, w2;

  if ((w1 = _rl_get_char_len (&buf1[pos1], ps1)) <= 0 || 
	(w2 = _rl_get_char_len (&buf2[pos2], ps2)) <= 0 ||
	(w1 != w2) ||
	(buf1[pos1] != buf2[pos2]))
    return 0;

  for (i = 1; i < w1; i++)
    if (buf1[pos1+i] != buf2[pos2+i])
      return 0;

  return 1;
}

/* adjust pointed byte and find mbstate of the point of string.
   adjusted point will be point <= adjusted_point, and returns
   differences of the byte(adjusted_point - point).
   if point is invalied (point < 0 || more than string length),
   it returns -1 */
int
_rl_adjust_point (string, point, ps)
     char *string;
     int point;
     mbstate_t *ps;
{
  size_t tmp = 0;
  int length;
  int pos = 0;

  length = strlen(string);
  if (point < 0)
    return -1;
  if (length < point)
    return -1;
  
  while (pos < point)
    {
      tmp = mbrlen (string + pos, length - pos, ps);
      if (MB_INVALIDCH ((size_t)tmp))
	{
	  /* in this case, bytes are invalid or shorted to compose
	     multibyte char, so assume that the first byte represents
	     a single character anyway. */
	  pos++;
	  /* clear the state of the byte sequence, because
	     in this case effect of mbstate is undefined  */
	  if (ps)
	    memset (ps, 0, sizeof (mbstate_t));
	}
      else if (MB_NULLWCH (tmp))
	pos++;
      else
	pos += tmp;
    }

  return (pos - point);
}

int
_rl_is_mbchar_matched (string, seed, end, mbchar, length)
     char *string;
     int seed, end;
     char *mbchar;
     int length;
{
  int i;

  if ((end - seed) < length)
    return 0;

  for (i = 0; i < length; i++)
    if (string[seed + i] != mbchar[i])
      return 0;
  return 1;
}

wchar_t
_rl_char_value (buf, ind)
     char *buf;
     int ind;
{
  size_t tmp;
  wchar_t wc;
  mbstate_t ps;
  int l;

  if (MB_LEN_MAX == 1 || rl_byte_oriented)
    return ((wchar_t) buf[ind]);
  l = strlen (buf);
  if (ind >= l - 1)
    return ((wchar_t) buf[ind]);
  memset (&ps, 0, sizeof (mbstate_t));
  tmp = mbrtowc (&wc, buf + ind, l - ind, &ps);
  if (MB_INVALIDCH (tmp) || MB_NULLWCH (tmp))  
    return ((wchar_t) buf[ind]);
  return wc;
}
#endif /* HANDLE_MULTIBYTE */

/* Find next `count' characters started byte point of the specified seed.
   If flags is MB_FIND_NONZERO, we look for non-zero-width multibyte
   characters. */
#undef _rl_find_next_mbchar
int
_rl_find_next_mbchar (string, seed, count, flags)
     char *string;
     int seed, count, flags;
{
#if defined (HANDLE_MULTIBYTE)
  return _rl_find_next_mbchar_internal (string, seed, count, flags);
#else
  return (seed + count);
#endif
}

/* Find previous character started byte point of the specified seed.
   Returned point will be point <= seed.  If flags is MB_FIND_NONZERO,
   we look for non-zero-width multibyte characters. */
#undef _rl_find_prev_mbchar
int
_rl_find_prev_mbchar (string, seed, flags)
     char *string;
     int seed, flags;
{
#if defined (HANDLE_MULTIBYTE)
  return _rl_find_prev_mbchar_internal (string, seed, flags);
#else
  return ((seed == 0) ? seed : seed - 1);
#endif
}

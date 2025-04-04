#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include <limits.h>
#include <wchar.h>

#include <time.h>

#include "putty.h"
#include "charset.h"
#include "terminal.h"
#include "misc.h"

/*
 * Unix Unicode-handling routines.
 */

bool is_dbcs_leadbyte(int codepage, char byte)
{
    return false;                      /* we don't do DBCS */
}

bool BinarySink_put_mb_to_wc(
    BinarySink *bs, int codepage, const char *mbstr, int mblen)
{
    if (codepage == DEFAULT_CODEPAGE) {
        mbstate_t state;

        memset(&state, 0, sizeof state);

        while (mblen > 0) {
            wchar_t wc;
            size_t i = mbrtowc(&wc, mbstr, (size_t)mblen, &state);
            if (i == (size_t)-1 || i == (size_t)-2)
                break;
            put_data(bs, &wc, sizeof(wc));
            mbstr += i;
            mblen -= i;
        }
    } else if (codepage == CS_NONE) {
        while (mblen > 0) {
            wchar_t wc = 0xD800 | (mbstr[0] & 0xFF);
            put_data(bs, &wc, sizeof(wc));
            mbstr++;
            mblen--;
        }
    } else {
        wchar_t wbuf[1024];
        while (mblen > 0) {
            int wlen = charset_to_unicode(&mbstr, &mblen, wbuf, lenof(wbuf),
                                          codepage, NULL, NULL, 0);
            put_data(bs, wbuf, wlen * sizeof(wchar_t));
        }
    }

    /* We never expect to receive invalid charset values on Unix,
     * because we're not dependent on an externally defined space of
     * OS-provided code pages */
    return true;
}

bool BinarySink_put_wc_to_mb(
    BinarySink *bs, int codepage, const wchar_t *wcstr, int wclen,
    const char *defchr)
{
    size_t defchr_len = 0;
    bool defchr_len_known = false;

    if (codepage == DEFAULT_CODEPAGE) {
        char output[MB_LEN_MAX];
        mbstate_t state;

        memset(&state, 0, sizeof state);

        while (wclen > 0) {
            size_t i = wcrtomb(output, wcstr[0], &state);
            if (i == (size_t)-1) {
                if (!defchr_len_known) {
                    defchr_len = strlen(defchr);
                    defchr_len_known = true;
                }
                put_data(bs, defchr, defchr_len);
            } else {
                put_data(bs, output, i);
            }
            wcstr++;
            wclen--;
        }
    } else if (codepage == CS_NONE) {
        while (wclen > 0) {
            if (*wcstr >= 0xD800 && *wcstr < 0xD900) {
                put_byte(bs, *wcstr & 0xFF);
            } else {
                if (!defchr_len_known) {
                    defchr_len = strlen(defchr);
                    defchr_len_known = true;
                }
                put_data(bs, defchr, defchr_len);
            }
            wcstr++;
            wclen--;
        }
    } else {
        char buf[2048];
        defchr_len = strlen(defchr);

        while (wclen > 0) {
            int len = charset_from_unicode(
                &wcstr, &wclen, buf, lenof(buf), codepage,
                NULL, defchr, defchr_len);
            put_data(bs, buf, len);
        }
    }

    return true;
}

/*
 * Return value is true if pterm is to run in direct-to-font mode.
 */
bool init_ucs(struct unicode_data *ucsdata, char *linecharset,
              bool utf8_override, int font_charset, int vtmode)
{
    int i;
    bool ret = false;

    /*
     * In the platform-independent parts of the code, font_codepage
     * is used only for system DBCS support - which we don't
     * support at all. So we set this to something which will never
     * be used.
     */
    ucsdata->font_codepage = -1;

    /*
     * If utf8_override is set and the POSIX locale settings
     * dictate a UTF-8 character set, then just go straight for
     * UTF-8.
     */
    ucsdata->line_codepage = CS_NONE;
    if (utf8_override) {
        const char *s;
        if (((s = getenv("LC_ALL"))   && *s) ||
            ((s = getenv("LC_CTYPE")) && *s) ||
            ((s = getenv("LANG"))     && *s)) {
            if (strstr(s, "UTF-8"))
                ucsdata->line_codepage = CS_UTF8;
        }
    }

    /*
     * Failing that, line_codepage should be decoded from the
     * specification in conf.
     */
    if (ucsdata->line_codepage == CS_NONE)
        ucsdata->line_codepage = decode_codepage(linecharset);

    /*
     * If line_codepage is _still_ CS_NONE, we assume we're using
     * the font's own encoding. This has been passed in to us, so
     * we use that. If it's still CS_NONE after _that_ - i.e. the
     * font we were given had an incomprehensible charset - then we
     * fall back to using the D800 page.
     */
    if (ucsdata->line_codepage == CS_NONE)
        ucsdata->line_codepage = font_charset;

    if (ucsdata->line_codepage == CS_NONE)
        ret = true;

    /*
     * Set up unitab_line, by translating each individual character
     * in the line codepage into Unicode.
     */
    for (i = 0; i < 256; i++) {
        char c[1];
        const char *p;
        wchar_t wc[1];
        int len;
        c[0] = i;
        p = c;
        len = 1;
        if (ucsdata->line_codepage == CS_NONE)
            ucsdata->unitab_line[i] = 0xD800 | i;
        else if (1 == charset_to_unicode(&p, &len, wc, 1,
                                         ucsdata->line_codepage,
                                         NULL, L"", 0))
            ucsdata->unitab_line[i] = wc[0];
        else
            ucsdata->unitab_line[i] = 0xFFFD;
    }

    /*
     * Set up unitab_xterm. This is the same as unitab_line except
     * in the line-drawing regions, where it follows the Unicode
     * encoding.
     *
     * (Note that the strange X encoding of line-drawing characters
     * in the bottom 32 glyphs of ISO8859-1 fonts is taken care of
     * by the font encoding, which will spot such a font and act as
     * if it were in a variant encoding of ISO8859-1.)
     */
    for (i = 0; i < 256; i++) {
        static const wchar_t unitab_xterm_std[32] = {
            0x2666, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
            0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba,
            0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c,
            0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7, 0x0020
        };
        static const wchar_t unitab_xterm_poorman[32] =
            L"*#****o~**+++++-----++++|****L. ";

        const wchar_t *ptr;

        if (vtmode == VT_POORMAN)
            ptr = unitab_xterm_poorman;
        else
            ptr = unitab_xterm_std;

        if (i >= 0x5F && i < 0x7F)
            ucsdata->unitab_xterm[i] = ptr[i & 0x1F];
        else
            ucsdata->unitab_xterm[i] = ucsdata->unitab_line[i];
    }

    /*
     * Set up unitab_scoacs. The SCO Alternate Character Set is
     * simply CP437.
     */
    for (i = 0; i < 256; i++) {
        char c[1];
        const char *p;
        wchar_t wc[1];
        int len;
        c[0] = i;
        p = c;
        len = 1;
        if (1 == charset_to_unicode(&p, &len, wc, 1, CS_CP437, NULL, L"", 0))
            ucsdata->unitab_scoacs[i] = wc[0];
        else
            ucsdata->unitab_scoacs[i] = 0xFFFD;
    }

    /*
     * Find the control characters in the line codepage. For
     * direct-to-font mode using the D800 hack, we assume 00-1F and
     * 7F are controls, but allow 80-9F through. (It's as good a
     * guess as anything; and my bet is that half the weird fonts
     * used in this way will be IBM or MS code pages anyway.)
     */
    for (i = 0; i < 256; i++) {
        int lineval = ucsdata->unitab_line[i];
        if (lineval < ' ' || (lineval >= 0x7F && lineval < 0xA0) ||
            (lineval >= 0xD800 && lineval < 0xD820) || (lineval == 0xD87F))
            ucsdata->unitab_ctrl[i] = i;
        else
            ucsdata->unitab_ctrl[i] = 0xFF;
    }

    return ret;
}

void init_ucs_generic(Conf *conf, struct unicode_data *ucsdata)
{
    init_ucs(ucsdata, conf_get_str(conf, CONF_line_codepage),
             conf_get_bool(conf, CONF_utf8_override),
             CS_NONE, conf_get_int(conf, CONF_vtmode));
}

const char *cp_name(int codepage)
{
    if (codepage == CS_NONE)
        return "Use font encoding";
    return charset_to_localenc(codepage);
}

const char *cp_enumerate(int index)
{
    int charset;
    charset = charset_localenc_nth(index);
    if (charset == CS_NONE) {
        /* "Use font encoding" comes after all the named charsets */
        if (charset_localenc_nth(index-1) != CS_NONE)
            return "Use font encoding";
        return NULL;
    }
    return charset_to_localenc(charset);
}

int decode_codepage(const char *cp_name)
{
    if (!cp_name || !*cp_name)
        return CS_UTF8;
    return charset_from_localenc(cp_name);
}

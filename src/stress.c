/*******************************************************************************
 *
 *                              Delta Chat Core
 *                      Copyright (C) 2017 Björn Petersen
 *                   Contact: r10s@b44t.com, http://b44t.com
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see http://www.gnu.org/licenses/ .
 *
 *******************************************************************************
 *
 * File:    stress.c
 * Purpose: Stress some functions for testing; if used as a lib, this file is
 *          obsolete.
 *
 *******************************************************************************
 *
 * For memory checking, use eg.
 * $ valgrind --leak-check=full --tool=memcheck ./deltachat-core <db>
 *
 ******************************************************************************/


#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include "mrmailbox.h"
#include "mrsimplify.h"
#include "mre2ee.h"
#include "mre2ee_driver.h"
#include "mrapeerstate.h"
#include "mraheader.h"
#include "mrtools.h"


void stress_functions(mrmailbox_t* mailbox)
{
	/* test mrsimplify and mrsaxparser (indirectly used by mrsimplify)
	 **************************************************************************/

	{
		mrsimplify_t* simplify = mrsimplify_new();

		const char* html = "\r\r\nline1<br>\r\n\r\n\r\rline2\n\r"; /* check, that `<br>\ntext` does not result in `\n text` */
		char* plain = mrsimplify_simplify(simplify, html, strlen(html), 1);
		assert( strcmp(plain, "line1\nline2")==0 );
		free(plain);

		html = "<a href=url>text</a"; /* check unquoted attribute and unclosed end-tag */
		plain = mrsimplify_simplify(simplify, html, strlen(html), 1);
		assert( strcmp(plain, "[text](url)")==0 );
		free(plain);

		html = "<!DOCTYPE name [<!DOCTYPE ...>]><!-- comment -->text <b><?php echo ... ?>bold</b><![CDATA[<>]]>";
		plain = mrsimplify_simplify(simplify, html, strlen(html), 1);
		assert( strcmp(plain, "text *bold*<>")==0 );
		free(plain);

		mrsimplify_unref(simplify);
	}

	/* test some string functions
	 **************************************************************************/

	{
		char* str = strdup("aaa");
		int replacements = mr_str_replace(&str, "a", "ab"); /* no endless recursion here! */
		assert( strcmp(str, "ababab")==0 );
		assert( replacements == 3 );
		free(str);

		str = mr_insert_breaks("just1234test", 4, " ");
		assert( strcmp(str, "just 1234 test")==0 );
		free(str);

		str = mr_insert_breaks("just1234tes", 4, "--");
		assert( strcmp(str, "just--1234--tes")==0 );
		free(str);

		str = mr_insert_breaks("just1234t", 4, "");
		assert( strcmp(str, "just1234t")==0 );
		free(str);

		str = mr_insert_breaks("", 4, "---");
		assert( strcmp(str, "")==0 );
		free(str);
	}

	/* test Autocrypt header parsing functions
	 **************************************************************************/

	{
		mraheader_t* ah = mraheader_new();
		char*        rendered = NULL;
		int          ah_ok;

		ah_ok = mraheader_set_from_string(ah, "to=a@b.example.org; type=p; prefer-encrypted=yes; key=RGVsdGEgQ2hhdA==");
		assert( ah_ok == 1 );
		assert( ah->m_to && strcmp(ah->m_to, "a@b.example.org")==0 );
		assert( ah->m_public_key.m_bytes==10 && strncmp((char*)ah->m_public_key.m_binary, "Delta Chat", 10)==0 );
		assert( ah->m_prefer_encrypted==MRA_PE_YES );

		rendered = mraheader_render(ah);
		assert( rendered && strcmp(rendered, "to=a@b.example.org; prefer-encrypted=yes; key= RGVsdGEgQ2hhdA==")==0 );

		ah_ok = mraheader_set_from_string(ah, " _foo; __FOO=BAR ;;; to = a@b.example.org ;\r\n type\r\n =\r\n p ; prefer-encrypted = yes ; key = RG VsdGEgQ\r\n2hhdA==");
		assert( ah_ok == 1 );
		assert( ah->m_to && strcmp(ah->m_to, "a@b.example.org")==0 );
		assert( ah->m_public_key.m_bytes==10 && strncmp((char*)ah->m_public_key.m_binary, "Delta Chat", 10)==0 );
		assert( ah->m_prefer_encrypted==MRA_PE_YES );

		ah_ok = mraheader_set_from_string(ah, "to=a@b.example.org; type=p; prefer-encrypted=nopreference; key=RGVsdGEgQ2hhdA==");
		assert( ah_ok == 0 ); /* only "yes" or "no" are valid for prefer-encrypted ... */

		ah_ok = mraheader_set_from_string(ah, "to=a@b.example.org; key=RGVsdGEgQ2hhdA==");
		assert( ah_ok == 1 && ah->m_prefer_encrypted==MRA_PE_NOPREFERENCE ); /* ... "nopreference" is use if the attribute is missing (see Autocrypt-Level0) */

		ah_ok = mraheader_set_from_string(ah, "");
		assert( ah_ok == 0 );

		ah_ok = mraheader_set_from_string(ah, ";");
		assert( ah_ok == 0 );

		ah_ok = mraheader_set_from_string(ah, "foo");
		assert( ah_ok == 0 );

		ah_ok = mraheader_set_from_string(ah, "\n\n\n");
		assert( ah_ok == 0 );

		ah_ok = mraheader_set_from_string(ah, " ;;");
		assert( ah_ok == 0 );

		ah_ok = mraheader_set_from_string(ah, "to=a@t.de; unknwon=1; key=jau"); /* unknwon non-underscore attributes result in invalid headers */
		assert( ah_ok == 0 );

		mraheader_unref(ah);
		free(rendered);
	}


	/* test end-to-end-encryption
	 **************************************************************************/

	{
		mrkey_t public_key, private_key;
		mrkey_init(&public_key);
		mrkey_init(&private_key);

		mre2ee_driver_create_keypair(mailbox, "f@f", &public_key, &private_key);

		char* temp = mrkey_render_base64(&public_key, 78, " ");
		char* tempsec = mrkey_render_base64(&private_key, 78, " ");
		printf("\nPUBLIC: [%s]\nPRIVATE: [%s]\n", temp, tempsec);
		free(temp); free(tempsec);

		mrkey_empty(&public_key);
		mrkey_empty(&private_key);

	}
}

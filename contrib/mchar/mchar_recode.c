#include "mchar.h"
#include "access/hash.h"

#include "unicode/ucol.h"
#include "unicode/ucnv.h"

static UConverter *cnvDB = NULL;
static UCollator  *colCaseInsensitive = NULL;
static UCollator  *colCaseSensitive = NULL;
static int         cnvMinCharSize = 0;
static int         cnvMaxCharSize = 0;
static bool        cnvInit = false;

static void
createUObjs() {
	UErrorCode err;

	if (cnvInit)
		return;

	err = U_ZERO_ERROR;
	cnvDB = ucnv_open(get_encoding_name_for_icu(GetDatabaseEncoding()), &err);
		
	if ( U_FAILURE(err) || cnvDB == NULL )
	{
		err = U_ZERO_ERROR;
		cnvDB = ucnv_open(NULL, &err);

		if ( U_FAILURE(err) || cnvDB == NULL ) 
			elog(ERROR,"ICU ucnv_open returns %d (%s)", err,  u_errorName(err));
	}

	colCaseInsensitive = ucol_open("", &err);
	if ( U_FAILURE(err) || cnvDB == NULL )
	{
		if ( colCaseSensitive )
			ucol_close( colCaseSensitive );
		colCaseSensitive = NULL;
		elog(ERROR,"ICU ucol_open returns %d (%s)", err,  u_errorName(err));
	}

	ucol_setStrength( colCaseInsensitive, UCOL_SECONDARY );

	colCaseSensitive = ucol_open("", &err);
	if ( U_FAILURE(err) || cnvDB == NULL )
	{
		if ( colCaseSensitive )
			ucol_close( colCaseSensitive );
		colCaseSensitive = NULL;
		elog(ERROR,"ICU ucol_open returns %d (%s)", err,  u_errorName(err));
	}

	ucol_setAttribute(colCaseSensitive, UCOL_CASE_FIRST, UCOL_UPPER_FIRST, &err);				
	if (U_FAILURE(err))
	{
		if ( colCaseSensitive )
			ucol_close( colCaseSensitive );
		colCaseSensitive = NULL;
		elog(ERROR,"ICU ucol_setAttribute returns %d (%s)", err,  u_errorName(err));
	}

	cnvMinCharSize = ucnv_getMinCharSize(cnvDB);
	if (!cnvMinCharSize)
		cnvMinCharSize = 1;
	
	cnvMaxCharSize = ucnv_getMaxCharSize(cnvDB);
	if (!cnvMaxCharSize)
		cnvMaxCharSize = 1;

	cnvInit = true;
}

int
Char2UCharMaxLength(int len)
{
	createUObjs();
	return (len + cnvMinCharSize - 1) / cnvMinCharSize;
}

int
UChar2CharMaxLength(int len)
{
	createUObjs();
	return UCNV_GET_MAX_BYTES_FOR_STRING(len, cnvMaxCharSize);
}

int
Char2UChar(const char * src, int srclen, UChar *dst, int dstlen) {
	UErrorCode err = 0;
	createUObjs();
	dstlen = ucnv_toUChars( cnvDB, dst, dstlen, src, srclen, &err ); 
	if ( U_FAILURE(err)) 
		elog(ERROR,"ICU ucnv_toUChars returns %d (%s)", err,  u_errorName(err));
	return dstlen;
}

int
UChar2Char(const UChar * src, int srclen, char *dst, int dstlen) {
	UErrorCode err = 0;
	createUObjs();
	dstlen = ucnv_fromUChars( cnvDB, dst, dstlen, src, srclen, &err ); 
	if ( U_FAILURE(err) )
		elog(ERROR,"ICU ucnv_fromUChars returns %d (%s)", err,  u_errorName(err));

	return dstlen;
}

int
UChar2Wchar(UChar * src, int srclen, pg_wchar *dst, int dstlen) {
	int		utflen = UChar2CharMaxLength(srclen);
	char	*utf = palloc(UChar2CharMaxLength(srclen));
	utflen = UChar2Char(src, srclen, utf, utflen);
	Assert(Char2UCharMaxLength(utflen)+1 <= dstlen);
	dstlen = pg_mb2wchar_with_len( utf, dst, utflen );
	pfree(utf);
	return dstlen;
}

static UChar UCharWhiteSpace = 0;

void
FillWhiteSpace( UChar *dst, int n ) {
	if ( UCharWhiteSpace == 0 ) {
		int len;
		UErrorCode err = 0;

		u_strFromUTF8( &UCharWhiteSpace, 1, &len, " ", 1, &err);

		Assert( len==1 );
		Assert( !U_FAILURE(err) );
	}

	while( n-- > 0 )
		*dst++ = UCharWhiteSpace;
}

int
UCharCaseCompare(UChar * a, int alen, UChar *b, int blen) {

	createUObjs();

	return (int)ucol_strcoll( colCaseInsensitive,
							  a, alen,
							  b, blen);
}

int
UCharCompare(UChar * a, int alen, UChar *b, int blen) {

	createUObjs();

	return  (int)ucol_strcoll( colCaseSensitive,
							  a, alen,
							  b, blen);
}

Datum
hash_uchar( UChar *s, int len ) {
	int32   length = INT_MAX, i;
	Datum res;
	uint8   *d;

	if ( len == 0 )
		return hash_any( NULL, 0 );

	createUObjs();

	for(i=2;; i*=2)
	{
		d =  palloc(len * i);
		length = ucol_getSortKey(colCaseInsensitive, s, len, d, len*i);

		if (length == 0)
			elog(ERROR,"ICU ucol_getSortKey fails");

		if (length < len*i)
			break;

		pfree(d);
	}

	res = hash_any( (unsigned char*) d,  length);

	pfree(d);

	return res;
}


#pragma once

#include <string>

#define MAX_PWD_LEN        10
#define LA_COUNT           26
#define UA_COUNT           26
#define NA_COUNT           24
#define NUM_COUNT          10
#define RANGE_LA_LOWER      0
#define RANGE_LA_UPPER     (LA_COUNT - 1)
#define RANGE_UA_LOWER     (LA_COUNT)
#define RANGE_UA_UPPER    ((LA_COUNT + UA_COUNT) - 1)
#define RANGE_NUM_LOWER    (LA_COUNT + UA_COUNT)
#define RANGE_NUM_UPPER   ((LA_COUNT + UA_COUNT + NUM_COUNT) - 1)
#define RANGE_NA_LOWER     (LA_COUNT + UA_COUNT + NUM_COUNT)
#define RANGE_NA_UPPER    ((LA_COUNT + UA_COUNT + NUM_COUNT + NA_COUNT) - 1)

enum {
    PERMUTE_SUCCESS     = 0,
    PERMUTE_DONE,
    PERMUTE_NO_OPTION
};

class PWD_NextUnit
{
private:
	const char lowerAlpha[LA_COUNT] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
								        'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z' };
	const char upperAlpha[UA_COUNT] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
								        'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z' };
	const char numeric[NUM_COUNT] =   { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };
	const char nonAlpha[NA_COUNT] =   { '~', '-', '=', '_', '$', '%', '+', '.', ';', ':', '[', '(', '{',
	                                    '}', ')', ']', '?', '@', '!', '#', '`', '^', '*', ',' };
	// removed: S quote, D quote, F slash, B slash, ampersand, greater, less, vertical
	char  testPassword[MAX_PWD_LEN + 1];
	int   testPwdLen;
	int   charIndicies[MAX_PWD_LEN];
	bool  useLAlpha;
	bool  useUAlpha;
	bool  useNAlpha;
	bool  useNumeric;
	bool  useNone;
	bool  permuteStatus = PERMUTE_SUCCESS;

public:
	PWD_NextUnit();
	~PWD_NextUnit();
	int     setNext();
	char*   get_plainPassword();
	void    set_useLAlpha(bool newValue);
	void    set_useUAlpha(bool newValue);
	void    set_useNAlpha(bool newValue);
	void    set_useNumeric(bool newValue);
    std::string  get_pwdAsIndicies();
    std::string  get_pwdAsText();
	int     get_testPwdLen();
	void    set_charIndicies(int charIdx, int charIdxValue);
	int     get_charIndicies(int charIdx);
	void    set_testPwdLen(int testLen);
	int     get_permuteStatus();
	void    set_permuteStatus(int status);

private:
	int     getLowestIndex();
};


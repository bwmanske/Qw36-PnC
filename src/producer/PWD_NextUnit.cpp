#include "PWD_NextUnit.h"


PWD_NextUnit::PWD_NextUnit()
{
	testPwdLen = 0;
	useLAlpha  = false;
	useUAlpha  = false;
	useNAlpha  = false;
	useNumeric = false;
	useNone    = true;

	for (int x = 0; x < MAX_PWD_LEN; x++) {
		testPassword[x] = 0;
		charIndicies[x] = -1;
	}
	testPassword[MAX_PWD_LEN] = 0;
}

PWD_NextUnit::~PWD_NextUnit()
{
}

int PWD_NextUnit::setNext()
{
	if (useNone) return PERMUTE_NO_OPTION;

	int localCharIndicies[MAX_PWD_LEN];
	if (permuteStatus == PERMUTE_SUCCESS) {
		for (int x = 0; x < MAX_PWD_LEN; x++) {
			localCharIndicies[x] = charIndicies[x];
		}
	}
	else {
		return PERMUTE_DONE;
	}

	bool done = false;
	int pwdCharIndex = 0;
	int charLookupIndex;
	int retVal = PERMUTE_SUCCESS;

	while (!done)
	{
		charLookupIndex = localCharIndicies[pwdCharIndex];
		if (charLookupIndex == -1) {
			localCharIndicies[pwdCharIndex] = getLowestIndex();
			charLookupIndex = getLowestIndex();
			done = true;
		}

		if (useLAlpha && !done) {
			if (charLookupIndex >= RANGE_LA_LOWER && charLookupIndex < RANGE_LA_UPPER) {
				localCharIndicies[pwdCharIndex]++;
				done = true;
			}
			else {
				if (charLookupIndex == RANGE_LA_UPPER) {
					if (useUAlpha) {
						localCharIndicies[pwdCharIndex] = RANGE_UA_LOWER;
						done = true;
						continue;
					}
					else if (useNumeric) {
						localCharIndicies[pwdCharIndex] = RANGE_NUM_LOWER;
						done = true;
						continue;
					}
					else if (useNAlpha) {
						localCharIndicies[pwdCharIndex] = RANGE_NA_LOWER;
						done = true;
						continue;
					}
					else {
						pwdCharIndex++;
						if (pwdCharIndex < MAX_PWD_LEN) {
							localCharIndicies[pwdCharIndex - 1] = getLowestIndex();
						}
						else {
							done = true;
							retVal = PERMUTE_DONE;
						}
					}
				}
			}
		}

		if (useUAlpha && !done) {
			if (charLookupIndex >= RANGE_UA_LOWER && charLookupIndex < RANGE_UA_UPPER) {
				localCharIndicies[pwdCharIndex]++;
				done = true;
			}
			else {
				if (charLookupIndex == RANGE_UA_UPPER) {
					if (useNumeric) {
						localCharIndicies[pwdCharIndex] = RANGE_NUM_LOWER;
						done = true;
						continue;
					}
					else if (useNAlpha) {
						localCharIndicies[pwdCharIndex] = RANGE_NA_LOWER;
						done = true;
						continue;
					}
					else {
						pwdCharIndex++;
						if (pwdCharIndex < MAX_PWD_LEN) {
							localCharIndicies[pwdCharIndex - 1] = getLowestIndex();
						}
						else {
							done = true;
							retVal = PERMUTE_DONE;
						}
					}
				}
			}
		}

		if (useNumeric && !done) {
			if (charLookupIndex >= RANGE_NUM_LOWER && charLookupIndex < RANGE_NUM_UPPER) {
				localCharIndicies[pwdCharIndex]++;
				done = true;
			}
			else {
				if (charLookupIndex == RANGE_NUM_UPPER) {
					if (useNAlpha) {
						localCharIndicies[pwdCharIndex] = RANGE_NA_LOWER;
						done = true;
						continue;
					}
					else {
						pwdCharIndex++;
						if (pwdCharIndex < MAX_PWD_LEN) {
							localCharIndicies[pwdCharIndex - 1] = getLowestIndex();
						}
						else {
							done = true;
							retVal = PERMUTE_DONE;
						}
					}
				}
			}
		}

		if (useNAlpha && !done) {
			if (charLookupIndex >= RANGE_NA_LOWER && charLookupIndex < RANGE_NA_UPPER) {
				localCharIndicies[pwdCharIndex]++;
				done = true;
			}
			else {
				if (charLookupIndex == RANGE_NA_UPPER) {
					pwdCharIndex++;
					if (pwdCharIndex < MAX_PWD_LEN) {
						localCharIndicies[pwdCharIndex - 1] = getLowestIndex();
					}
					else {
						done = true;
						retVal = PERMUTE_DONE;
					}
				}
			}
		}
	}

	if (retVal == PERMUTE_SUCCESS) {
		for (int x = 0; x < MAX_PWD_LEN; x++) {
			charIndicies[x] = localCharIndicies[x];
		}

		if (pwdCharIndex + 1 > testPwdLen)
			testPwdLen = pwdCharIndex + 1;
	}
	else {
		permuteStatus = retVal;
	}

	return retVal;
}

std::string PWD_NextUnit::get_pwdAsIndicies()
{
	std::string pwdStr = std::to_string(testPwdLen);
	for (int x = testPwdLen - 1; x >= 0; x--) {
		pwdStr += "," + std::to_string(charIndicies[x]);
	}

	return pwdStr;
}

std::string PWD_NextUnit::get_pwdAsText()
{
	std::string pwdStr = std::to_string(testPwdLen) + " chars ";
	for (int x = testPwdLen-1; x >= 0; x--) {
		if (charIndicies[x] >= RANGE_LA_LOWER && charIndicies[x] <= RANGE_LA_UPPER) {
			pwdStr += lowerAlpha[charIndicies[x] - RANGE_LA_LOWER];
		}
		else if (charIndicies[x] >= RANGE_UA_LOWER && charIndicies[x] <= RANGE_UA_UPPER) {
			pwdStr += upperAlpha[charIndicies[x] - RANGE_UA_LOWER];
		}
		else if (charIndicies[x] >= RANGE_NUM_LOWER && charIndicies[x] <= RANGE_NUM_UPPER) {
			pwdStr += numeric[charIndicies[x] - RANGE_NUM_LOWER];
		}
		else if (charIndicies[x] >= RANGE_NA_LOWER && charIndicies[x] <= RANGE_NA_UPPER) {
			char tmpChar = nonAlpha[charIndicies[x] - RANGE_NA_LOWER];
			switch (tmpChar) {
			case '~':
				pwdStr += "tilda";
				break;
			case '-':
				pwdStr += "hyphen";
				break;
			case '=':
				pwdStr += "equal";
				break;
			case '_':
				pwdStr += "underscore";
				break;
			case '$':
				pwdStr += "dollar";
				break;
			case '%':
				pwdStr += "percent";
				break;
			case '+':
				pwdStr += "plus";
				break;
			case '.':
				pwdStr += "period";
				break;
			case ';':
				pwdStr += "semicolon";
				break;
			case ':':
				pwdStr += "colon";
				break;
			case '[':
				pwdStr += "L brace";
				break;
			case '(':
				pwdStr += "L parenthesis";
				break;
			case '{':
				pwdStr += "L curly";
				break;
			case '}':
				pwdStr += "R curly";
				break;
			case ')':
				pwdStr += "R parenthesis";
				break;
			case ']':
				pwdStr += "R brace";
				break;
			case '?':
				pwdStr += "question";
				break;
			case '@':
				pwdStr += "at";
				break;
			case '!':
				pwdStr += "exclamation";
				break;
			case '#':
				pwdStr += "number";
				break;
			case '`':
				pwdStr += "grave";
				break;
			case '^':
				pwdStr += "caret";
				break;
			case '*':
				pwdStr += "asterisk";
				break;
			case ',':
				pwdStr += "comma";
				break;
			default:
				pwdStr += "ERROR " + std::to_string((int)tmpChar);
			}
		}
		if (x != 0) pwdStr += ",";
	}

	return pwdStr;
}

int PWD_NextUnit::getLowestIndex()
{
	if (useLAlpha)  return RANGE_LA_LOWER;
	if (useUAlpha)  return RANGE_UA_LOWER;
	if (useNumeric) return RANGE_NUM_LOWER;
	if (useNAlpha)  return RANGE_NA_LOWER;
	return -1;
}

char* PWD_NextUnit::get_plainPassword()
{
	int x, y;
	int charLookupIndex = 0;
	for (y = 0, x = testPwdLen-1; x >= 0; x--, y++) {
		charLookupIndex = charIndicies[x];
		if (charLookupIndex >= RANGE_LA_LOWER && charLookupIndex <= RANGE_LA_UPPER) {
			testPassword[y] = lowerAlpha[charLookupIndex - RANGE_LA_LOWER];
		}
		else if (charLookupIndex >= RANGE_UA_LOWER && charLookupIndex <= RANGE_UA_UPPER) {
			testPassword[y] = upperAlpha[charLookupIndex - RANGE_UA_LOWER];
		}
		else if (charLookupIndex >= RANGE_NUM_LOWER && charLookupIndex <= RANGE_NUM_UPPER) {
			testPassword[y] = numeric[charLookupIndex - RANGE_NUM_LOWER];
		}
		else if (charLookupIndex >= RANGE_NA_LOWER && charLookupIndex <= RANGE_NA_UPPER) {
			testPassword[y] = nonAlpha[charLookupIndex - RANGE_NA_LOWER];
		}
		else
			return nullptr;
	}
	testPassword[testPwdLen] = 0;
	return testPassword;
}

void PWD_NextUnit::set_useLAlpha(bool newValue)
{
	useLAlpha = newValue;
	useNone = !(useLAlpha || useUAlpha || useNumeric || useNAlpha);
}

void PWD_NextUnit::set_useUAlpha(bool newValue)
{
	useUAlpha = newValue;
	useNone = !(useLAlpha || useUAlpha || useNumeric || useNAlpha);
}

void PWD_NextUnit::set_useNAlpha(bool newValue)
{
	useNAlpha = newValue;
	useNone = !(useLAlpha || useUAlpha || useNumeric || useNAlpha);
}

void PWD_NextUnit::set_useNumeric(bool newValue)
{
	useNumeric = newValue;
	useNone = !(useLAlpha || useUAlpha || useNumeric || useNAlpha);
}

void PWD_NextUnit::set_charIndicies(int charIdx, int charIdxValue)
{
	charIndicies[charIdx] = charIdxValue;
}

int PWD_NextUnit::get_charIndicies(int charIdx)
{
	return charIndicies[charIdx];
}

int PWD_NextUnit::get_permuteStatus()
{
	return permuteStatus;
}

void PWD_NextUnit::set_permuteStatus(int status)
{
	permuteStatus = status;
}

void PWD_NextUnit::set_testPwdLen(int testLen)
{
	testPwdLen = testLen;
}

int PWD_NextUnit::get_testPwdLen()
{
	return testPwdLen;
}

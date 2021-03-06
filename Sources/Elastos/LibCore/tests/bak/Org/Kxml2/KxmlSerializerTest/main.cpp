#include "test.h"
#include <stdio.h>
#include <stdlib.h>
#include <elautoptr.h>

typedef int (CTest::*PTestEntry)(int argc, char *argv[]);

PTestEntry TestEntry[] =
{
    &CTest::testWhitespaceInAttributeValue,
    &CTest::testWriteDocument,
    &CTest::testWriteSpecialCharactersInText,
    &CTest::testInvalidCharactersInText,
    &CTest::testInvalidCharactersInAttributeValues,
    &CTest::testInvalidCharactersInCdataSections,
    &CTest::testCdataWithTerminatorInside,
};

int main(int argc, char* argv[])
{
    printf("%s %d\n", __FILE__, __LINE__);
    int nIndex = atoi(argv[1]);
    printf("%s %d\n", __FILE__, __LINE__);
    CTest testObj;
    (testObj.*TestEntry[nIndex-1])(argc, argv);

    return 0;
}

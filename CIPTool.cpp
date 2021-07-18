// CIP Tool - tool for Yu-Gi-Oh! Tag Force CIP/CPM files (Card Image Pack?)
// by Xan

// My assumptions
// CIP = Card Image Pack
// CPM = Card Picture Middle
// CPJ = ???

#include <iostream>
#include <ctype.h>

#ifdef WIN32
#include <windows.h>
#include <strsafe.h>
#endif

#define CIP_MAGICNUM 0x1A504943
#define CPM_MAGICNUM 0x1A4D5043
#define CPJ_MAGICNUM 0x1A4A5043
#define CIP_DEFAULTSIZE 0x1000
#define DEFAULT_HEADER_FILENAME "header.bin"

char OutputFileName[1024];
char TempStringBuffer[1024];
char* AutogenFolderName;
wchar_t MkDirPath[1024];

// pack mode stuff
char** FileDirectoryListing;
unsigned int GIMFileCount = 0;
unsigned int AltArtCount = 0;

struct CIPHeader
{
    unsigned int MagicNum;
    unsigned int FullFileSize;
    unsigned int Padding1[2];
    unsigned int HeaderSize;
    unsigned int MinCardNumber;
    unsigned int MaxCardNumber;
    unsigned int OffsetTableTop;
    unsigned int BitshiftSize;
    unsigned int Padding2[3];
}InputCIPHeader;

struct CIPOffsetPair
{
    unsigned int ID;
    unsigned int Offset;
}EndPair = {0xFFFFFFFF, 0xFFFFFFFF};

struct CIPPackerStruct
{
    char* Filename;
    bool bIsAltArt;
    unsigned int CardID;
    unsigned int* GIMFileOffset; // per alt art
    unsigned int AltArtOffset;
    unsigned int AltArtCount;
    unsigned int OffsetPairOffset;
    CIPOffsetPair* OffsetPair; // per alt art
}*CIPPackerInfo;


#ifdef WIN32
DWORD GetDirectoryListing(const char* FolderPath) // platform specific code, using Win32 here, GNU requires use of dirent which MSVC doesn't have -- TODO - make crossplat variant
{
    WIN32_FIND_DATA ffd = { 0 };
    TCHAR  szDir[MAX_PATH];
    char MBFilename[MAX_PATH];
    HANDLE hFind = INVALID_HANDLE_VALUE;
    DWORD dwError = 0;
    unsigned int NameCounter = 0;

    mbstowcs(szDir, FolderPath, MAX_PATH);
    StringCchCat(szDir, MAX_PATH, TEXT("\\*"));

    if (strlen(FolderPath) > (MAX_PATH - 3))
    {
        printf("Directory path is too long.\n");
        return -1;
    }

    hFind = FindFirstFile(szDir, &ffd);

    if (INVALID_HANDLE_VALUE == hFind)
    {
        printf("FindFirstFile error\n");
        return dwError;
    }

    // count the files up first
    do
    {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            GIMFileCount++;
        }
    } while (FindNextFile(hFind, &ffd) != 0);

    dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES)
    {
        printf("FindFirstFile error\n");
    }
    FindClose(hFind);

    // then create a file list in an array, redo the code
    FileDirectoryListing = (char**)calloc(GIMFileCount, sizeof(char*));

    ffd = { 0 };
    hFind = FindFirstFile(szDir, &ffd);
    if (INVALID_HANDLE_VALUE == hFind)
    {
        printf("FindFirstFile error\n");
        return dwError;
    }

    do
    {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            wcstombs(MBFilename, ffd.cFileName, MAX_PATH);
            FileDirectoryListing[NameCounter] = (char*)calloc(strlen(MBFilename) + 1, sizeof(char));
            strcpy(FileDirectoryListing[NameCounter], MBFilename);
            if (strchr(MBFilename, '_'))
            {
                AltArtCount++;
            }
            NameCounter++;
        }
    } while (FindNextFile(hFind, &ffd) != 0);

    dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES)
    {
        printf("FindFirstFile error\n");
    }

    FindClose(hFind);
    return dwError;
}
#else
void GetDirectoryListing(const char* FolderPath)
{
    printf("Directory listing unimplemented for non-Win32 platforms.\n");
}
#endif

unsigned int FindMinCardNumber()
{
    unsigned int ReadNum = 0;
    unsigned int SmallestNum = 0xFFFFFFFF;
    unsigned int Dummy = 0;
    for (unsigned int i = 0; i < GIMFileCount; i++)
    {
        if (strchr(FileDirectoryListing[i], '_'))
            sscanf(FileDirectoryListing[i], "%d_%d.gim", &ReadNum, &Dummy);
        else
            sscanf(FileDirectoryListing[i], "%d.gim", &ReadNum);
        if (ReadNum < SmallestNum)
            SmallestNum = ReadNum;
    }
    return SmallestNum;
}

unsigned int FindMaxCardNumber()
{
    unsigned int ReadNum = 0;
    unsigned int BiggestNum = 0;
    unsigned int Dummy = 0;
    for (unsigned int i = 0; i < GIMFileCount; i++)
    {
        if (strchr(FileDirectoryListing[i], '_'))
            sscanf(FileDirectoryListing[i], "%d_%d.gim", &ReadNum, &Dummy);
        else
            sscanf(FileDirectoryListing[i], "%d.gim", &ReadNum);
        if (ReadNum > BiggestNum)
            BiggestNum = ReadNum;
    }
    return BiggestNum;
}

bool bFileExists(const char* Filename)
{
    FILE* chk = fopen(Filename, "rb");
    if (!chk)
        return false;
    fclose(chk);
    return true;
}

unsigned int SearchFilenameIndex(char** NameList, char* Name)
{
    unsigned int result = 0;

    for (unsigned int i = 0; i < GIMFileCount; i++)
    {
        if (strcmp(NameList[i], Name) == 0)
            result = i;
    }

    return result;
}

unsigned int CountAltArtFiles(const char* InFolder, unsigned int CardID)
{
    unsigned int count = 0;

    while (1)
    {
        sprintf(TempStringBuffer, "%s\\%d_%d.gim", InFolder, CardID, count);
        if (!bFileExists(TempStringBuffer))
            break;

        count++;
    }

    return count;
}


int PackCIP(const char* InFolder, const char* OutFilename)
{
    FILE* fin = NULL;
    // open the output file
    FILE* fout = fopen(OutFilename, "wb");
    struct stat st = { 0 };

    unsigned int OffsetTableSize = 0;
    unsigned int AltOffsetTableSize = 0;
    unsigned int FirstOffset = 0;
    unsigned int ReadCounter = 0;
    unsigned int ReadCounter2 = 0;
    unsigned int OffsetPairCounter = 0;
    unsigned int PackerPrepCounter = 0;
    void* InFileBuffer = 0;

    if (!fout)
    {
        printf("ERROR: Can't open file %s for writing!\n", OutFilename);
        perror("ERROR");
        return -1;
    }

    // get dir list + count up files and alt art ones
    GetDirectoryListing(InFolder);

    // prepare header
    InputCIPHeader.MagicNum = CIP_MAGICNUM;
    InputCIPHeader.MinCardNumber = FindMinCardNumber();
    InputCIPHeader.MaxCardNumber = FindMaxCardNumber();
    InputCIPHeader.OffsetTableTop = sizeof(CIPHeader);

    // get size of first GIM to get the bitshift size -- TODO: make this find the largest GIM and get closest bitshift size to its size
    sprintf(TempStringBuffer, "%s\\%s", InFolder, FileDirectoryListing[0]);
    if (stat(TempStringBuffer, &st))
    {
        printf("ERROR: Can't open %s for stat reading!\n", TempStringBuffer);
        return -1;
    }
    InputCIPHeader.BitshiftSize = st.st_size >> 11;
    OffsetTableSize = ((InputCIPHeader.MaxCardNumber - InputCIPHeader.MinCardNumber) << 3) + 8;
    AltOffsetTableSize = AltArtCount * 8;
    InputCIPHeader.HeaderSize = sizeof(CIPHeader) + OffsetTableSize + AltOffsetTableSize + 8;

    printf("Header size: 0x%X \n", InputCIPHeader.HeaderSize);
    printf("Min Card Number: %d \n", InputCIPHeader.MinCardNumber);
    printf("Max Card Number: %d \n", InputCIPHeader.MaxCardNumber);
    printf("Offset Table Top: 0x%X \n", InputCIPHeader.OffsetTableTop);
    printf("Bitshift size: 0x%X \n", InputCIPHeader.BitshiftSize);

    FirstOffset = ((InputCIPHeader.HeaderSize + 0x7FF) & 0xFFFFF800);

    // prepare some info for packing...
    CIPPackerInfo = (CIPPackerStruct*)calloc(InputCIPHeader.MaxCardNumber - InputCIPHeader.MinCardNumber + 1, sizeof(CIPPackerStruct));

    for (unsigned int i = InputCIPHeader.MinCardNumber; i <= InputCIPHeader.MaxCardNumber; i++)
    {
        sprintf(TempStringBuffer, "%s\\%d.gim", InFolder, i);
        CIPPackerInfo[PackerPrepCounter].CardID = i;
        CIPPackerInfo[PackerPrepCounter].GIMFileOffset = (unsigned int*)calloc(1, sizeof(int));
        CIPPackerInfo[PackerPrepCounter].OffsetPair = (CIPOffsetPair*)calloc(1, sizeof(CIPOffsetPair));
        if (bFileExists(TempStringBuffer))
        {
            sprintf(TempStringBuffer, "%d.gim", i);
            CIPPackerInfo[PackerPrepCounter].Filename = FileDirectoryListing[SearchFilenameIndex(FileDirectoryListing, TempStringBuffer)];
        }
        if (AltArtCount)
        {
            sprintf(TempStringBuffer, "%s\\%d_0.gim", InFolder, i);
            if (bFileExists(TempStringBuffer))
            {
                sprintf(TempStringBuffer, "%d_0.gim", i);
                CIPPackerInfo[PackerPrepCounter].Filename = FileDirectoryListing[SearchFilenameIndex(FileDirectoryListing, TempStringBuffer)];
                CIPPackerInfo[PackerPrepCounter].bIsAltArt = true;
                CIPPackerInfo[PackerPrepCounter].AltArtCount = CountAltArtFiles(InFolder, i);
                free(CIPPackerInfo[PackerPrepCounter].GIMFileOffset);
                free(CIPPackerInfo[PackerPrepCounter].OffsetPair);
                CIPPackerInfo[PackerPrepCounter].GIMFileOffset = (unsigned int*)calloc(CIPPackerInfo[PackerPrepCounter].AltArtCount, sizeof(int));
                CIPPackerInfo[PackerPrepCounter].OffsetPair = (CIPOffsetPair*)calloc(CIPPackerInfo[PackerPrepCounter].AltArtCount, sizeof(CIPOffsetPair));
            }
        }

        PackerPrepCounter++;
    }

    // start calculating offset IDs for each card art (alt art ones are taken note of already in the previous step)
    for (unsigned int i = InputCIPHeader.MinCardNumber; i <= InputCIPHeader.MaxCardNumber; i++)
    {
        if (CIPPackerInfo[OffsetPairCounter].Filename)
        {
            if (CIPPackerInfo[OffsetPairCounter].AltArtCount > 1)
            {
                for (unsigned int j = 0; j < CIPPackerInfo[OffsetPairCounter].AltArtCount; j++)
                {
                    CIPPackerInfo[OffsetPairCounter].GIMFileOffset[j] = (FirstOffset) + ((InputCIPHeader.BitshiftSize << 11) * (ReadCounter2 + ReadCounter));
                    CIPPackerInfo[OffsetPairCounter].OffsetPair[j].ID = (FirstOffset >> 11) + ((InputCIPHeader.BitshiftSize) * (ReadCounter2 + ReadCounter));
                    if (j == CIPPackerInfo[OffsetPairCounter].AltArtCount - 1)
                        CIPPackerInfo[OffsetPairCounter].OffsetPair[j].ID += 0x80000000;
                    ReadCounter2++;
                    if (ReadCounter2 == 10)
                    {
                        ReadCounter2 = 0;
                        ReadCounter += 10;
                    }
                }
            }
            else
            {
                *CIPPackerInfo[OffsetPairCounter].GIMFileOffset = (FirstOffset) + ((InputCIPHeader.BitshiftSize << 11) * (ReadCounter2 + ReadCounter));
                CIPPackerInfo[OffsetPairCounter].OffsetPair->ID = (FirstOffset >> 11) + ((InputCIPHeader.BitshiftSize) * (ReadCounter2 + ReadCounter));
                ReadCounter2++;
                if (ReadCounter2 == 10)
                {
                    ReadCounter2 = 0;
                    ReadCounter += 10;
                }
            }
        }
        OffsetPairCounter++;
    }

    // start writing to file
    // write header
    fwrite(&InputCIPHeader, sizeof(CIPHeader), 1, fout);
    
    // write offset table pairs for single art cards and do alt art ones later
    OffsetPairCounter = 0;
    for (unsigned int i = InputCIPHeader.MinCardNumber; i <= InputCIPHeader.MaxCardNumber; i++)
    {
        CIPPackerInfo[OffsetPairCounter].OffsetPairOffset = ((i - InputCIPHeader.MinCardNumber) << 3) + InputCIPHeader.OffsetTableTop;
        if (!CIPPackerInfo[OffsetPairCounter].bIsAltArt)
        {
            fseek(fout, CIPPackerInfo[OffsetPairCounter].OffsetPairOffset, SEEK_SET);
            fwrite(CIPPackerInfo[OffsetPairCounter].OffsetPair, sizeof(CIPOffsetPair), 1, fout);
        }
        OffsetPairCounter++;
    }

    // write alt art offset pairs now
    OffsetPairCounter = 0;
    for (unsigned int i = InputCIPHeader.MinCardNumber; i <= InputCIPHeader.MaxCardNumber; i++)
    {
        if (CIPPackerInfo[OffsetPairCounter].bIsAltArt)
        {
            CIPPackerInfo[OffsetPairCounter].AltArtOffset = ftell(fout) + 0x80000000;
            fwrite(CIPPackerInfo[OffsetPairCounter].OffsetPair, sizeof(CIPOffsetPair), CIPPackerInfo[OffsetPairCounter].AltArtCount, fout);
        }
        OffsetPairCounter++;
    }
    fwrite(&EndPair, sizeof(CIPOffsetPair), 1, fout);

    // finally, write pointers to the alt art offset pairs themselves where they're supposed to go
    OffsetPairCounter = 0;
    for (unsigned int i = InputCIPHeader.MinCardNumber; i <= InputCIPHeader.MaxCardNumber; i++)
    {
        if (CIPPackerInfo[OffsetPairCounter].bIsAltArt)
        {
            fseek(fout, CIPPackerInfo[OffsetPairCounter].OffsetPairOffset, SEEK_SET);
            fwrite(&CIPPackerInfo[OffsetPairCounter].AltArtOffset, sizeof(int), 1, fout);
        }
        OffsetPairCounter++;
    }

    // FULL HEADER IS DONE

    // start writing the GIMs themselves to the file
    OffsetPairCounter = 0;
    InFileBuffer = malloc(InputCIPHeader.BitshiftSize << 11);
    for (unsigned int i = InputCIPHeader.MinCardNumber; i <= InputCIPHeader.MaxCardNumber; i++)
    {
        if (CIPPackerInfo[OffsetPairCounter].Filename)
        {
            if (CIPPackerInfo[OffsetPairCounter].AltArtCount > 1)
            {
                for (unsigned int j = 0; j < CIPPackerInfo[OffsetPairCounter].AltArtCount; j++)
                {
                    sprintf(TempStringBuffer, "%s\\%d_%d.gim", InFolder, CIPPackerInfo[OffsetPairCounter].CardID, j);
                    printf("Writing: %d_%d (0x%X @0x%X) %s\n", CIPPackerInfo[OffsetPairCounter].CardID, j, CIPPackerInfo[OffsetPairCounter].OffsetPair[j].ID & 0x00FFFFFF, CIPPackerInfo[OffsetPairCounter].GIMFileOffset[j], TempStringBuffer);
                    fin = fopen(TempStringBuffer, "rb");
                    if (!fin)
                    {
                        printf("ERROR: Can't open %s for reading!\n", TempStringBuffer);
                        perror("ERROR");
                        return -1;
                    }
                    memset(InFileBuffer, 0, InputCIPHeader.BitshiftSize << 11);
                    fread(InFileBuffer, InputCIPHeader.BitshiftSize << 11, 1, fin);
                    fseek(fout, CIPPackerInfo[OffsetPairCounter].GIMFileOffset[j], SEEK_SET);
                    fwrite(InFileBuffer, InputCIPHeader.BitshiftSize << 11, 1, fout);
                    fclose(fin);
                }
            }
            else
            {
                sprintf(TempStringBuffer, "%s\\%s", InFolder, CIPPackerInfo[OffsetPairCounter].Filename);
                printf("Writing: %d (0x%X @0x%X) %s\n", CIPPackerInfo[OffsetPairCounter].CardID, CIPPackerInfo[OffsetPairCounter].OffsetPair->ID, *CIPPackerInfo[OffsetPairCounter].GIMFileOffset, TempStringBuffer);
                fin = fopen(TempStringBuffer, "rb");
                if (!fin)
                {
                    printf("ERROR: Can't open %s for reading!\n", TempStringBuffer);
                    perror("ERROR");
                    return -1;
                }
                memset(InFileBuffer, 0, InputCIPHeader.BitshiftSize << 11);
                fread(InFileBuffer, InputCIPHeader.BitshiftSize << 11, 1, fin);
                fseek(fout, *CIPPackerInfo[OffsetPairCounter].GIMFileOffset, SEEK_SET);
                fwrite(InFileBuffer, InputCIPHeader.BitshiftSize << 11, 1, fout);
                fclose(fin);
            }
        }
        OffsetPairCounter++;
    }
    InputCIPHeader.FullFileSize = ftell(fout);
    fseek(fout, 4, SEEK_SET);
    fwrite(&InputCIPHeader.FullFileSize, sizeof(int), 1, fout);


    free(InFileBuffer);
    fclose(fout);
    return 0;
}

int ExtractCIP(const char* InFilename, const char* OutFolder)
{
    FILE* fin = fopen(InFilename, "rb");
    FILE* fout = NULL;

    if (!fin)
    {
        printf("ERROR: Can't open %s for reading!\n", InFilename);
        perror("ERROR");
        return -1;
    }

    unsigned int ReadCounter = 0;
    unsigned int CardOffsetID = 0;
    unsigned int DataOffset = 0;
    unsigned int MemoryCursor = 0;
    unsigned int ReadBuffer = 0;
    unsigned int GIMFileSize = 0;
    unsigned int BlockSize = 0;
    unsigned int BlockCount = 0;
    unsigned int FirstOffset = 0;
    CIPOffsetPair* OffsetPair = NULL;
    void* HeaderBuffer = 0;
    void* GIMBuffer = 0;
    bool bHeaderEnd = false;
    struct stat st = { 0 };

    // check for folder existence, if it doesn't exist, make it
    if (stat(OutFolder, &st))
    {
        printf("Creating folder: %s\n", OutFolder);
        mbstowcs(MkDirPath, OutFolder, 1024);
        _wmkdir(MkDirPath);
    }

    if (stat(InFilename, &st))
    {
        printf("ERROR: Can't open %s for stat reading!\n", InFilename);
        return -1;
    }

    fread(&InputCIPHeader, sizeof(CIPHeader), 1, fin);

    printf("Header size: 0x%X \n", InputCIPHeader.HeaderSize);
    printf("Min Card Number: %d \n", InputCIPHeader.MinCardNumber);
    printf("Max Card Number: %d \n", InputCIPHeader.MaxCardNumber);
    printf("Offset Table Top: 0x%X \n", InputCIPHeader.OffsetTableTop);
    printf("Bitshift size: 0x%X \n", InputCIPHeader.BitshiftSize);
    GIMFileSize = InputCIPHeader.BitshiftSize << 11;
    BlockSize = GIMFileSize * 10;
    BlockCount = st.st_size / BlockSize;

    fseek(fin, 0, SEEK_SET);
    HeaderBuffer = malloc(InputCIPHeader.HeaderSize);
    fread(HeaderBuffer, InputCIPHeader.HeaderSize, 1, fin);

    GIMBuffer = malloc(GIMFileSize);

    FirstOffset = ((InputCIPHeader.HeaderSize + 0x7FF) & 0xFFFFF800);

    // do it the way the game does it - calculcate offset pairs and write them into the header itself
    for (unsigned int j = 0; j < BlockCount; j++)
    {
        for (unsigned int i = 0; i < 10; i++)
        {
            MemoryCursor = 0;
            DataOffset = FirstOffset + ((InputCIPHeader.BitshiftSize << 11) * (i + ReadCounter));
            CardOffsetID = (FirstOffset >> 11) + ((InputCIPHeader.BitshiftSize) * (i + ReadCounter));

            //printf("Searching CardID: %d\n", CardID);

            while(1)
            {
               ReadBuffer = *(int*)((int)HeaderBuffer + InputCIPHeader.OffsetTableTop + MemoryCursor);
               if (ReadBuffer == 0xFFFFFFFF)
                   break;
               ReadBuffer <<= 1;
               ReadBuffer >>= 1;
               if (CardOffsetID == ReadBuffer)
               {
                   *(int*)((int)HeaderBuffer + InputCIPHeader.OffsetTableTop + MemoryCursor + 4) = DataOffset;
               }
               MemoryCursor += 8;
            }
           
        }
        ReadCounter += 10;
    }

    // for every card ID, lookup the offset table & rip out every art for it (if there are multiple)
    for (unsigned int i = InputCIPHeader.MinCardNumber; i <= InputCIPHeader.MaxCardNumber; i++)
    {
        bool bLastMultiArt = false;
        unsigned int SubArtCounter = 0;
        OffsetPair = (CIPOffsetPair*)((int)HeaderBuffer + ((i - InputCIPHeader.MinCardNumber) << 3) + InputCIPHeader.OffsetTableTop);
        if (OffsetPair->ID & 0x80000000) // multi art card
        {
            OffsetPair = (CIPOffsetPair*)((OffsetPair->ID & 0x00FFFFFF) + (int)HeaderBuffer);
            do
            {
                if (OffsetPair->ID & 0x80000000)
                    bLastMultiArt = true;


                if (!(OffsetPair->Offset == 0))
                {
                    fseek(fin, OffsetPair->Offset, SEEK_SET);
                    sprintf(OutputFileName, "%s\\%d_%d.gim", OutFolder, i, SubArtCounter);
                    printf("Writing: %d_%d (0x%X @0x%X) %s\n", i, SubArtCounter, OffsetPair->ID & 0x00FFFFFF, OffsetPair->Offset, OutputFileName);
                    fout = fopen(OutputFileName, "wb");
                    if (!fout)
                    {
                        printf("ERROR: Can't open %s for writing!\n", OutputFileName);
                        perror("ERROR");
                        if (fin)
                            fclose(fin);
                        return -1;
                    }
                    memset(GIMBuffer, 0, GIMFileSize);
                    fread(GIMBuffer, GIMFileSize, 1, fin);
                    fwrite(GIMBuffer, GIMFileSize, 1, fout);
                    fclose(fout);
                }

                OffsetPair += 1;
                SubArtCounter++;
            } while (!bLastMultiArt);

        }
        else
        {
            if (!(OffsetPair->Offset == 0))
            {
                fseek(fin, OffsetPair->Offset, SEEK_SET);
                sprintf(OutputFileName, "%s\\%d.gim", OutFolder, i);
                printf("Writing: %d (0x%X @0x%X) %s\n", i, OffsetPair->ID, OffsetPair->Offset, OutputFileName);
                fout = fopen(OutputFileName, "wb");
                if (!fout)
                {
                    printf("ERROR: Can't open %s for writing!\n", OutputFileName);
                    perror("ERROR");
                    if (fin)
                        fclose(fin);
                    return -1;
                }
                memset(GIMBuffer, 0, GIMFileSize);
                fread(GIMBuffer, GIMFileSize, 1, fin);
                fwrite(GIMBuffer, GIMFileSize, 1, fout);
                fclose(fout);
            }
        }
    }

    free(GIMBuffer);
    free(HeaderBuffer);
    fclose(fin);
    return 0;
}

int DetectAndExtract(const char* InFilename, const char* OutFolder)
{
    FILE* fin = fopen(InFilename, "rb");

    printf("Opening: %s\n", InFilename);

    if (!fin)
    {
        printf("ERROR: Can't open %s for reading during type detection!\n", InFilename);
        perror("ERROR");
        return -1;
    }

    unsigned int CIPMagic = 0;
    fread(&CIPMagic, sizeof(int), 1, fin);
    fclose(fin);

    switch (CIPMagic)
    {
    case CIP_MAGICNUM:
        printf("Detected a CIP file!\n");
        ExtractCIP(InFilename, OutFolder);
        break;
    case CPM_MAGICNUM:
        printf("Detected a CPM (middle) file!\n");
        printf("Unimplemented...\n");
        break;
    case CPJ_MAGICNUM:
        printf("Detected a CPJ file!\n");
        printf("Unimplemented...\n");
        break;
    default:
        printf("This is an invalid file! Read file magic: 0x%X\n", CIPMagic);
        return -1;
    }
    return 0;
}


int main(int argc, char* argv[])
{
    printf("Yu-Gi-Oh! Tag Force CIP Tool\n");

    if (argc < 2)
    {
        printf("USAGE (extract): InFile.cip [OutFolder]\nUSAGE (pack): InFolder OutCIP.cip\n");
        return 0;
    }

    if (argv[1][0] == '-' && argv[1][1] == 'p')
    {
        printf("Packing mode\n");
        PackCIP(argv[2], argv[3]);
        return 0;
    }

    printf("Extraction mode\n");
    if (argc == 2) // Extraction mode
    {
        char* PatchPoint;
        AutogenFolderName = (char*)calloc(strlen(argv[1]), sizeof(char));
        strcpy(AutogenFolderName, argv[1]);
        PatchPoint = strrchr(AutogenFolderName, '.');
        *PatchPoint = 0;
    }
    else
        AutogenFolderName = argv[2];

    DetectAndExtract(argv[1], AutogenFolderName);

    return 0;
}

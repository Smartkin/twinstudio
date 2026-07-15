#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <rpmalloc.h>
#include <raylib.h>
#include <string.h>

#include "memory/memory.h"
#include "parser.h"

static const char usageHint[] = "Usage: twinres (-d) [INPUT] (OUTPUT_DESTINATION)\n"
"Flags:\n"
"\t-d Provide a directory instead of a single file to regenerate all containing *.tr files. Search is recursive\n";


static int WriteOutputFiles(TwinRes_GeneratedFile* files, const char* genLocation)
{
    for (int32_t i = 0; i < TwinRes_MaxGenFiles; ++i)
    {
        if (files[i].length == 0)
        {
            continue;
        }

        char resultFilePath[8192];
        snprintf(resultFilePath, sizeof(resultFilePath), "%s/%s", genLocation, files[i].fileName);
        FILE* newGenFile = fopen(resultFilePath, "w");
        if (newGenFile == NULL)
        {
            fprintf(stderr, "Error creating auto generated file! %s", resultFilePath);
            return 1;
        }

        int written = fprintf(newGenFile, TR_PARSER_FILE_FORMAT, TR_PARSER_FILE_ARG(files[i]));
        fclose(newGenFile);

        printf("Finished writing generated file %s\n", resultFilePath);
    }

    return 0;
}


static int GenerateOutput(const char* inputFilePath, const char* genDestination)
{
    int exitCode = 1;

    FILE* inputFile = fopen(inputFilePath, "rb");
    if (inputFile == NULL)
    {
        fprintf(stderr, "Error opening input file %s", inputFilePath);
        return exitCode;
    }

    const char* genLocation = genDestination;
    const char* baseName = GetFileNameWithoutExt(inputFilePath);
    const int fileSize = GetFileLength(inputFilePath);

    char* stringData = TWIN_MALLOC(fileSize + 1);
    memset(stringData, 0, fileSize + 1);
    fread(stringData, sizeof(char), fileSize, inputFile);
    stringData[fileSize] = '\0';
    fclose(inputFile);

    TwinRes_GeneratorOutput output = TwinRes_ParseAndGenerate(baseName, stringData);
    if (output.status == TwinRes_GeneratorFail)
    {
        fprintf(stderr, "Error creating auto generated file!\n%s: %s\n", baseName, output.errorString);
        goto generatorCleanup;
    }

    int writeStatus = WriteOutputFiles(output.structFiles, genLocation);
    if (writeStatus != 0)
    {
        goto generatorCleanup;
    }

    writeStatus = WriteOutputFiles(output.uiFiles, genLocation);
    if (writeStatus != 0)
    {
        goto generatorCleanup;
    }

    exitCode = 0;

generatorCleanup:
    TwinRes_FreeGeneratedOutput(&output);
    TWIN_FREE(stringData);

    return exitCode;
}


int main(int argc, char** argv)
{
    rpmalloc_initialize(NULL);

    int exitCode = 1;
    if (argc < 2)
    {
        fprintf(stderr, "Incorrect arguments.\n");
        fprintf(stderr, usageHint);
        return exitCode;
    }

    const bool useDirectory = (strcmp(argv[1], "-d") == 0);
    if (useDirectory)
    {
        FilePathList files = LoadDirectoryFilesEx(argv[2], ".tr", true);
        for (uint32_t i = 0; i < files.count; ++i)
        {
            exitCode = GenerateOutput(files.paths[i], argc > 3 ? argv[3] : GetDirectoryPath(files.paths[i]));
            if (exitCode != 0)
            {
                break;
            }
        }

        UnloadDirectoryFiles(files);
    }
    else
    {
        exitCode = GenerateOutput(argv[1], argc > 2 ? argv[2] : GetDirectoryPath(argv[1]));
    }

    rpmalloc_finalize();

    return exitCode;
}
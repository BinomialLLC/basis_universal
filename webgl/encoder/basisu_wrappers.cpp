// basis_wrappers.cpp - Simple C-style wrappers to the C++ encoder for WebGL use.
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <basisu_enc.h>

using namespace emscripten;

bool readFile(const std::string &filename, std::vector <unsigned char>& data)
{
    FILE *pFile = nullptr;
#ifdef _WIN32
	fopen_s(&pFile, filename.c_str(), "r");
#else
	pFile = fopen(filename.c_str(), "r");
#endif
	if (!pFile)
	{
		basisu::error_printf("Failed to open file for reading: \"%s\"\n", filename.c_str());
		return false;
	}

    fseek(pFile, 0, SEEK_END);
    long len = ftell(pFile);
    if (len <= 0)
    {
        basisu::error_printf("Failed to determine output size for file: \"%s\"\n", filename.c_str());
        return false;
    }

    fseek(pFile, 0, SEEK_SET);
    data.resize(len);
    if (fread(&data[0], 1, len, pFile) != (size_t) len)
    {
        basisu::error_printf("Failed to read file: \"%s\"\n", filename.c_str());
        return false;
    }
    fclose(pFile);
    return true;
}

bool writeFile(const std::string &filename, const std::string &data)
{
    FILE *pFile = nullptr;
#ifdef _WIN32
	fopen_s(&pFile, filename.c_str(), "wb");
#else
	pFile = fopen(filename.c_str(), "wb");
#endif
	if (!pFile)
	{
		basisu::error_printf("Failed to open file for writing: \"%s\"\n", filename.c_str());
		return false;
	}

    fwrite(&data[0], 1, data.size(), pFile);
    fclose(pFile);

    return true;
}

int main(int argc, const char **argv);

val compress(const std::string &inputData, int compressionLevel)
{
    static std::vector<unsigned char> outputData;
    const char *inputFilename = "input.png";
    const char *outputFilename = "input.basis";

    // store the input file
    if (!writeFile(inputFilename, inputData))
    {
        return val(0);
    }

    std::string compLevel(1, '0' + compressionLevel);

    // compress
    std::vector<const char *> params =
    {
        "basisu",
        inputFilename,
        "-comp_level", compLevel.c_str(),
        "-no_multithreading"
    };
    if (main(params.size(), &params[0]) != 0)
    {
        return val(0);
    }

    // read resulting basis file
    if (!readFile(outputFilename, outputData))
    {
        return val(0);
    }

    return val(typed_memory_view(outputData.size(), &outputData[0]));
}

EMSCRIPTEN_BINDINGS(basis_encoder) {
    function("compress", &compress);
}

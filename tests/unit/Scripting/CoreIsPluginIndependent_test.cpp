// The scripting core serves every scripted plugin: nothing in scripting/ may include a plugin or name one.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace jp::script::test
{

namespace fs = std::filesystem;

TEST(ScriptingCore, NoFileOfTheCoreIncludesAPluginOrNamesOne)
{
    const fs::path directory = fs::path(JP_SOURCE_DIR) / "scripting";
    ASSERT_TRUE(fs::is_directory(directory));
    size_t files = 0;
    for (const auto& entry : fs::directory_iterator(directory))
    {
        const std::string extension = entry.path().extension().string();
        if (extension != ".h" && extension != ".cpp")
        {
            continue;
        }
        ++files;
        std::ifstream in(entry.path());
        std::string line;
        size_t number = 0;
        while (std::getline(in, line))
        {
            ++number;
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const std::string where = entry.path().filename().string() + ":" + std::to_string(number);
            EXPECT_EQ(line.find("#include \"plugins/"), std::string::npos) << where << ": " << line;
            for (const char* name : {"morphex", "shipahoy", "eigenghost", "jaystwin", "midiscript"})
            {
                EXPECT_EQ(lower.find(name), std::string::npos) << where << " names a plugin: " << line;
            }
        }
    }
    EXPECT_GE(files, 7u);
}

}

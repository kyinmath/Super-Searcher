//mostly stolen from stackexchange http://stackoverflow.com/questions/5815227/fix-improve-word-wrap-function
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

void WordWrap(const string& inputString, vector<string>& outputString, unsigned int lineLength)
{
	istringstream iss(inputString);

	string line;

	do
	{
		if (iss.peek() == '\n')
		{
			outputString.push_back(line);
			line.clear();
			iss.ignore(1);
			continue;
		}
		string word;
		iss >> word;

		if (line.length() + word.length() > lineLength)
		{
			outputString.push_back(line + '\\');
			line.clear();
		}
		line += word + " ";

	} while (iss);

	if (!line.empty())
	{
		outputString.push_back(line);
	}
}

//stolen from http://stackoverflow.com/questions/612097/how-can-i-get-the-list-of-files-in-a-directory-using-c-or-c
#include <Windows.h>

vector<string> get_all_files_names_within_folder(string folder)
{
	vector<string> names;
	char search_path[200];
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
	sprintf_s(search_path, "%s*.*", folder.c_str());
	WIN32_FIND_DATA fd;
	HANDLE hFind = ::FindFirstFile(search_path, &fd);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			// read all (real) files in current folder
			// , delete '!' read other 2 default folder . and ..
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				names.push_back(fd.cFileName);
			}
		} while (::FindNextFile(hFind, &fd));
		::FindClose(hFind);
	}
	return names;
}

#include <fstream>
#include <streambuf>
//stolen from http://stackoverflow.com/questions/2602013/read-whole-ascii-file-into-c-stdstring
int main()
{
	vector<string> k = get_all_files_names_within_folder("src/");
	for (auto &x : k)
	{
		std::ifstream t(x);
		std::string str;

		t.seekg(0, std::ios::end);
		str.reserve(t.tellg());
		t.seekg(0, std::ios::beg);

		str.assign((std::istreambuf_iterator<char>(t)),
			std::istreambuf_iterator<char>());
		vector<string> wrapped;
		WordWrap(str, wrapped, 100);

		std::ofstream outfile("/wrapped/" + x);

		for (auto &y : wrapped)
		{
			// write to outfile
			outfile.write(y.c_str(), y.size());
			outfile.write("\n", 1);
		}
		outfile.close();
		return 0;
	}
}
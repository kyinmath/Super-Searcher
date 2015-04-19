//mostly stolen from stackexchange http://stackoverflow.com/questions/5815227/fix-improve-word-wrap-function
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

using namespace std;

void WordWrap(ifstream& iss, vector<string>& outputString, unsigned int lineLength)
{
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
		if (iss.peek() == '\t')
		{
			line.append("  ");
			iss.ignore(1);
			continue;
		}
		string word;
		iss >> word;

		if (line.length() + word.length() + 2 > lineLength)
		{
			outputString.push_back(line + "\\");
			line.clear();
		}
		line += word + " ";
		if (line.length() > lineLength)
		{
			//cout << "how";
		}

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

#include <streambuf>
//stolen from http://stackoverflow.com/questions/2602013/read-whole-ascii-file-into-c-stdstring
int main()
{
	vector<string> k = get_all_files_names_within_folder("C:\\Users\\CompAcc\\Stuff\\CS11backend\\src\\");
	for (auto &x : k)
	{
		std::ifstream t("C:\\Users\\CompAcc\\Stuff\\CS11backend\\src\\" + x);
		if (!t.is_open()) cerr << "Error: " << strerror(errno);
		cout << "C:\\Users\\CompAcc\\Stuff\\CS11backend\\src\\" + x << '\n';
		vector<string> wrapped;
		WordWrap(t, wrapped, 100);

		std::ofstream outfile("C:\\Users\\CompAcc\\Stuff\\CS11backend\\wrapped\\"  + x);
		if (!outfile.is_open())
			cout << "panic!\n";
		cout << "C:\\Users\\CompAcc\\Stuff\\CS11backend\\wrapped\\" + x << '\n';

		for (auto &y : wrapped)
		{
			// write to outfile
			outfile.write(y.c_str(), y.size());
			outfile.write("\n", 1);
		}
		outfile.close();
		t.close();
	}
	cin.get();
}
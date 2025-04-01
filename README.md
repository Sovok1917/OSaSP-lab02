# Dirwalk - Directory Traversal Utility

**Dirwalk** is a command-line utility written in C that performs directory traversal, listing files, directories, and symbolic links based on specified options. It supports filtering the output to include only files, directories, or links, and provides an option to sort the output.

---

## Features

- **Directory Traversal**: Recursively traverses directories starting from a specified path (default is the current directory).
- **Filter Output**:
  - List only symbolic links (`-l`).
  - List only directories (`-d`).
  - List only files (`-f`).
- **Sort Output**: Sort the output alphabetically using locale-specific collation (`-s`).
- **Combined Options**: Combine options (e.g., `-ld` to list both links and directories).
- **Error Handling**: Provides meaningful error messages for invalid options or unexpected arguments.

## Building

Build the app with following:
- 1 Clone the repository:
  - ```git clone https://github.com/Sovok1917/dirwalk/```
- 2 Build the project:
  - ```make MODE=release```
- 3 Running the app (either one works):
  - ```./build/release/prog [options] [directory]```
  - or
  - ```./build/release/prog [directory] [options]```


#pragma once
#include <string>

struct PyWorker  {
    pid_t pid = -1;
    FILE* to_python = nullptr;
    FILE* from_python = nullptr;
};

void pyworker_start(PyWorker *w, const char *script_path);
std::string pyworker_embed(PyWorker *w, const std::string &text);
void pyworker_stop(PyWorker *w);

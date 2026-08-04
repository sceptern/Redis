#include "python_worker.h"
#include <iostream>

int main() {
    PyWorker worker;

    std::cout << "Starting python worker...\n";
    pyworker_start(&worker, "python_worker.py");

    std::string result = pyworker_embed(&worker, "Dogs are cute");
    std::cout << "Response: " << result;

    pyworker_stop(&worker);
    return 0;
}
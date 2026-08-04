#include "python_worker.h"
#include <unistd.h>
#include <sys/wait.h>
#include <stdexcept>
#include <iostream>

void pyworker_start(PyWorker* w, const char* script_path) {
    int to_python_fds[2];
    int from_python_fds[2];

    if (pipe(to_python_fds) != 0 || pipe(from_python_fds) != 0)
        throw std::runtime_error("pipe() failed");

    w->pid = fork();
    if (w->pid < 0) {
        throw std::runtime_error("fork() failed");
    }

    if (w->pid == 0) {

        dup2(to_python_fds[0], STDIN_FILENO);
        dup2(from_python_fds[1], STDOUT_FILENO);

        close(to_python_fds[0]);
        close(to_python_fds[1]);
        close(from_python_fds[0]);
        close(from_python_fds[1]);

        execlp("python3", "python3", script_path, NULL);
        perror("execlp failed");
        _exit(1);
    }

    
    close(to_python_fds[0]);
    close(from_python_fds[1]);

    w->to_python = fdopen(to_python_fds[1], "w");
    w->from_python = fdopen(from_python_fds[0], "r");

    if (!w->to_python || !w->from_python)
        throw std::runtime_error("fdopen() failed");

    
    char buf[4096];
    if (!fgets(buf, sizeof(buf), w->from_python))
        throw std::runtime_error("python worker died before sending ready signal");

    std::cout << "[python_worker] ready signal: " << buf;
}


static std::string json_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

std::string pyworker_embed(PyWorker *w, const std::string &text) {
    if (!w->to_python || !w->from_python)
        throw std::runtime_error("worker not started");

    
    std::string request = "{\"text\": \"" + json_escape(text) + "\"}\n";

    fputs(request.c_str(), w->to_python);
    fflush(w->to_python);

    char buf[65536]; 
    if (!fgets(buf, sizeof(buf), w->from_python))
        throw std::runtime_error("python worker died mid-request");

    return std::string(buf);
}

void pyworker_stop(PyWorker *w) {
    if (w->to_python) {
        fclose(w->to_python);
        w->to_python = NULL;
    }
    if (w->from_python) {
        fclose(w->from_python);
        w->from_python = NULL;
    }
    if (w->pid > 0) {
        int status;
        waitpid(w->pid, &status, 0);
        w->pid = -1;
    }
}
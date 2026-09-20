#include "IPC.hpp"
#include <stdexcept>

using namespace std;

namespace ipc {
string erroWindows(const string& contexto) {
    return contexto + " (erro Windows " + to_string(GetLastError()) + ").";
}

string paraUtf8(const wstring& texto) {
    if (texto.empty()) {
        return {};
    }

    const int tamanho = WideCharToMultiByte(CP_UTF8,
        WC_ERR_INVALID_CHARS,
        texto.data(),
        static_cast<int>(texto.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (tamanho == 0) {
        throw runtime_error(erroWindows("Converter texto"));
    }

    string resultado(tamanho, '\0');
    WideCharToMultiByte(CP_UTF8,
        WC_ERR_INVALID_CHARS,
        texto.data(),
        static_cast<int>(texto.size()),
        resultado.data(),
        tamanho,
        nullptr,
        nullptr);
    return resultado;
}

Handle criarPipe() {
    Handle pipe(CreateNamedPipeW(NOME_PIPE,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES,
        LIMITE_MENSAGEM,
        LIMITE_MENSAGEM,
        5000,
        nullptr));

    if (!pipe.valido()) {
        throw runtime_error(erroWindows("Criar pipe"));
    }

    return pipe;
}

Handle conectar() {
    // Aguarda uma instancia livre, sem manter o cliente preso para sempre na conexao.
    const auto inicio = GetTickCount64();

    do {
        Handle pipe(CreateFileW(NOME_PIPE,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr));

        if (pipe.valido()) {
            DWORD modo = PIPE_READMODE_MESSAGE;

            if (!SetNamedPipeHandleState(pipe.get(), &modo, nullptr, nullptr)) {
                throw runtime_error(erroWindows("Configurar pipe"));
            }

            return pipe;
        }

        const DWORD erro = GetLastError();

        if (erro != ERROR_PIPE_BUSY && erro != ERROR_FILE_NOT_FOUND) {
            throw runtime_error(erroWindows("Conectar"));
        }

        Sleep(20);
    } while (GetTickCount64() - inicio < 5000);

    throw runtime_error("Servidor indisponivel. Inicie Servidor.exe primeiro.");
}

string receber(HANDLE pipe) {
    char buffer[LIMITE_MENSAGEM];
    DWORD bytes = 0;

    if (!ReadFile(pipe, buffer, sizeof(buffer), &bytes, nullptr) || bytes == 0) {
        throw runtime_error(erroWindows("Ler mensagem"));
    }

    return string(buffer, bytes);
}

void enviar(HANDLE pipe, const string& mensagem) {
    if (mensagem.empty() || mensagem.size() > LIMITE_MENSAGEM) {
        throw runtime_error("Mensagem deve conter entre 1 e 4096 bytes.");
    }

    DWORD bytes = 0;

    if (!WriteFile(pipe,
            mensagem.data(),
            static_cast<DWORD>(mensagem.size()),
            &bytes,
            nullptr) ||
        bytes != mensagem.size()) {
        throw runtime_error(erroWindows("Enviar mensagem"));
    }
}

void responder(HANDLE pipe, const string& mensagem) {
    enviar(pipe, mensagem);

    // Aguarda o cliente ler a resposta antes de fechar esta conexao, sem ACK proprio.
    if (!FlushFileBuffers(pipe)) {
        throw runtime_error(erroWindows("Concluir resposta"));
    }

    DisconnectNamedPipe(pipe);
}
} // namespace ipc

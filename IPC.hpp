#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <utility>

using namespace std;

namespace ipc {
inline constexpr wchar_t NOME_PIPE[] = LR"(\\.\pipe\so_banco_paralelo)";
inline constexpr DWORD LIMITE_MENSAGEM = 4096;

// Dono de um recurso do Windows: fecha automaticamente ao sair do escopo.
class Handle {
public:
    explicit Handle(HANDLE valor = INVALID_HANDLE_VALUE) : valor_(valor) {
    }

    ~Handle() {
        if (valido()) {
            CloseHandle(valor_);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& outro) noexcept
        : valor_(exchange(outro.valor_, INVALID_HANDLE_VALUE)) {
    }

    Handle& operator=(Handle&& outro) noexcept {
        if (this != &outro) {
            if (valido()) {
                CloseHandle(valor_);
            }

            valor_ = exchange(outro.valor_, INVALID_HANDLE_VALUE);
        }

        return *this;
    }

    HANDLE get() const {
        return valor_;
    }

    bool valido() const {
        return valor_ != nullptr && valor_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE valor_;
};

string erroWindows(const string& contexto);
string paraUtf8(const wstring& texto);
Handle criarPipe();
Handle conectar();
string receber(HANDLE pipe);
void enviar(HANDLE pipe, const string& mensagem);
void responder(HANDLE pipe, const string& mensagem);
} // namespace ipc

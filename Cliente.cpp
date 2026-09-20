#include "IPC.hpp"
#include <iostream>
#include <stdexcept>

using namespace std;

void mostrarAjuda() {
    cout << "\nINSERT 1 Ana       - inserir\n"
         << "SELECT 1           - consultar\n"
         << "UPDATE 1 Ana Maria - atualizar\n"
         << "DELETE 1           - remover\n"
         << "AJUDA              - mostrar comandos\n"
         << "SAIR               - fechar este cliente\n"
         << "ENCERRAR           - concluir tarefas e encerrar o servidor\n\n";
}

bool executar(const string& comando) {
    auto pipe = ipc::conectar();
    ipc::enviar(pipe.get(), comando);
    const string resposta = ipc::receber(pipe.get());
    cout << resposta << '\n';
    return resposta.rfind("OK:", 0) == 0;
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    try {
        if (argc > 1) {
            string comando;

            for (int i = 1; i < argc; ++i) {
                if (i > 1) {
                    comando += ' ';
                }

                comando += ipc::paraUtf8(argv[i]);
            }

            return executar(comando) ? 0 : 1;
        }

        cout << "BANCO PARALELO | Cliente PID " << GetCurrentProcessId() << '\n';
        mostrarAjuda();
        string comando;

        while (cout << "> " && getline(cin, comando)) {
            if (comando == "SAIR") {
                break;
            }

            if (comando == "AJUDA") {
                mostrarAjuda();
                continue;
            }

            if (comando.empty()) {
                continue;
            }

            try {
                const bool sucesso = executar(comando);

                if (comando == "ENCERRAR" && sucesso) {
                    break;
                }
            } catch (const exception& erro) {
                cerr << "ERRO: " << erro.what() << '\n';
            }
        }

        return 0;
    } catch (const exception& erro) {
        cerr << "ERRO: " << erro.what() << '\n';
        return 1;
    }
}

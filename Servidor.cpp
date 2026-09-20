#include "IPC.hpp"
#include "Banco.hpp"
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std;

namespace {
struct Tarefa {
    ipc::Handle pipe;
    string comando;
};

// O receptor produz tarefas; as threads do pool consomem.
class Fila {
public:
    void adicionar(Tarefa tarefa) {
        lock_guard<mutex> trava(mutex_);
        tarefas_.push(move(tarefa));
        disponivel_.notify_one();
    }

    bool retirar(Tarefa& tarefa) {
        unique_lock<mutex> trava(mutex_);
        disponivel_.wait(trava, [this] {
            return encerrada_ || !tarefas_.empty();
        });

        if (tarefas_.empty()) {
            return false;
        }

        tarefa = move(tarefas_.front());
        tarefas_.pop();
        return true;
    }

    void encerrar() {
        lock_guard<mutex> trava(mutex_);
        encerrada_ = true;
        disponivel_.notify_all();
    }

private:
    queue<Tarefa> tarefas_;
    mutex mutex_;
    condition_variable disponivel_;
    bool encerrada_ = false;
};

// A destruicao automatica libera o semaforo mesmo em caso de erro.
class GuardaSemaforo {
public:
    explicit GuardaSemaforo(HANDLE semaforo) : semaforo_(semaforo) {
        if (WaitForSingleObject(semaforo_, INFINITE) != WAIT_OBJECT_0) {
            throw runtime_error(ipc::erroWindows("Adquirir semaforo"));
        }
    }

    ~GuardaSemaforo() {
        ReleaseSemaphore(semaforo_, 1, nullptr);
    }

    GuardaSemaforo(const GuardaSemaforo&) = delete;
    GuardaSemaforo& operator=(const GuardaSemaforo&) = delete;

private:
    HANDLE semaforo_;
};

void registrar(ofstream& log,
    const string& comando,
    const string& resposta,
    int worker,
    long long tempoUs) {
    SYSTEMTIME agora{};
    GetLocalTime(&agora);

    // quoted impede que aspas e barras tornem os campos ambiguos.
    // Controles sao substituidos para preservar uma linha por requisicao.
    string texto = comando;

    for (char& caractere : texto) {
        if (static_cast<unsigned char>(caractere) < 32) {
            caractere = ' ';
        }
    }

    log << setfill('0') << setw(4) << agora.wYear << '-' << setw(2) << agora.wMonth
        << '-' << setw(2) << agora.wDay << ' ' << setw(2) << agora.wHour << ':'
        << setw(2) << agora.wMinute << ':' << setw(2) << agora.wSecond
        << " | worker=" << worker << " | thread=" << GetCurrentThreadId()
        << " | comando=" << quoted(texto) << " | " << resposta
        << " | tempo_us=" << tempoUs << '\n';
    log.flush();

    if (!log) {
        throw runtime_error(
            "Falha no log; confira o banco antes de repetir a operacao.");
    }
}

void processar(Fila& fila, Tabela& banco, ofstream& log, HANDLE semaforo, int worker) {
    Tarefa tarefa;

    while (fila.retirar(tarefa)) {
        const auto inicio = chrono::steady_clock::now();
        string resposta;
        Requisicao requisicao;

        try {
            requisicao = interpretar(tarefa.comando);
        } catch (const exception& erro) {
            resposta = "ERRO: " + string(erro.what());
        }

        try {
            GuardaSemaforo guarda(semaforo);

            if (resposta.empty()) {
                try {
                    resposta = banco.executar(requisicao);
                } catch (const exception& erro) {
                    resposta = "ERRO: " + string(erro.what());
                }
            }

            const auto tempoUs = chrono::duration_cast<chrono::microseconds>(
                chrono::steady_clock::now() - inicio)
                                     .count();
            registrar(log, tarefa.comando, resposta, worker, tempoUs);
            cout << "Worker " << worker << " | " << resposta << '\n' << flush;
        } catch (const exception& erro) {
            resposta = "ERRO: " + string(erro.what());
        }

        // O envio pode bloquear; por isso acontece depois de liberar o semaforo.
        try {
            resposta += " | Worker " + to_string(worker);
            ipc::responder(tarefa.pipe.get(), resposta);
        } catch (const exception& erro) {
            cerr << "IPC: " << erro.what() << '\n';
        }

        tarefa.pipe = ipc::Handle{};
    }
}
} // namespace

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    try {
        int quantidade = 4;

        if (argc > 2) {
            throw runtime_error("Uso: Servidor.exe [threads 1..16]");
        }

        if (argc == 2) {
            const string texto = argv[1];
            const auto resultado =
                from_chars(texto.data(), texto.data() + texto.size(), quantidade);

            if (resultado.ec != errc{} ||
                resultado.ptr != texto.data() + texto.size() || quantidade < 1 ||
                quantidade > 16) {
                throw runtime_error("Use de 1 a 16 threads.");
            }
        }

        // Impede dois servidores de alterarem o mesmo banco por semaforos diferentes.
        ipc::Handle instancia(
            CreateMutexW(nullptr, FALSE, L"Local\\so_banco_paralelo_servidor"));

        if (!instancia.valido() || GetLastError() == ERROR_ALREADY_EXISTS) {
            throw runtime_error("Ja existe servidor ativo ou falhou a inicializacao.");
        }

        Tabela banco;
        ofstream log("log.txt", ios::app | ios::binary);
        ipc::Handle semaforo(CreateSemaphoreW(nullptr, 1, 1, nullptr));

        if (!log || !semaforo.valido()) {
            throw runtime_error("Nao foi possivel abrir o log ou criar o semaforo.");
        }

        Fila fila;
        vector<thread> workers;
        int codigo = 0;

        try {
            for (int i = 1; i <= quantidade; ++i) {
                workers.emplace_back(
                    processar, ref(fila), ref(banco), ref(log), semaforo.get(), i);
            }

            cout << "BANCO PARALELO | Servidor PID " << GetCurrentProcessId()
                 << " | Threads: " << quantidade << "\nBanco: "
                 << ipc::paraUtf8(filesystem::absolute("banco.json").wstring())
                 << "\nLog: "
                 << ipc::paraUtf8(filesystem::absolute("log.txt").wstring())
                 << "\nDigite ENCERRAR no cliente para concluir e sair.\n"
                 << flush;

            while (true) {
                auto pipe = ipc::criarPipe();

                if (!ConnectNamedPipe(pipe.get(), nullptr) &&
                    GetLastError() != ERROR_PIPE_CONNECTED) {
                    throw runtime_error(ipc::erroWindows("Aceitar cliente"));
                }

                try {
                    string comando = ipc::receber(pipe.get());

                    if (comando == "ENCERRAR") {
                        ipc::responder(pipe.get(),
                            "OK: encerramento solicitado; concluindo a fila.");
                        break;
                    }

                    fila.adicionar({move(pipe), move(comando)});
                } catch (const exception& erro) {
                    cerr << "IPC: " << erro.what() << '\n';
                }
            }
        } catch (const exception& erro) {
            cerr << "ERRO: " << erro.what() << '\n';
            codigo = 1;
        }

        fila.encerrar();

        for (auto& worker : workers) {
            worker.join();
        }

        cout << "Servidor encerrado; tarefas concluidas.\n";
        return codigo;
    } catch (const exception& erro) {
        cerr << "ERRO: " << erro.what() << '\n';
        return 1;
    }
}

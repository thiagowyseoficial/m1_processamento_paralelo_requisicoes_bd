#include "IPC.hpp"
#include "Banco.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

using namespace std;
namespace fs = filesystem;

namespace {
bool textoValido(const string& texto, size_t limite) {
    if (texto.empty() || texto.size() > limite) {
        return false;
    }

    bool temConteudo = false;

    for (unsigned char caractere : texto) {
        if (caractere < 32 || caractere == 127) {
            return false;
        }

        if (caractere != ' ') {
            temConteudo = true;
        }
    }

    return temConteudo && MultiByteToWideChar(CP_UTF8,
                              MB_ERR_INVALID_CHARS,
                              texto.data(),
                              static_cast<int>(texto.size()),
                              nullptr,
                              0) > 0;
}

// Leitor do formato produzido por salvar(), sem implementar JSON generico.
void esperar(istream& arquivo, char esperado) {
    arquivo >> ws;

    if (arquivo.get() != esperado) {
        throw runtime_error("Formato invalido em banco.json.");
    }
}

string lerTexto(istream& arquivo) {
    esperar(arquivo, '"');
    string texto;
    char caractere;

    while (arquivo.get(caractere)) {
        if (caractere == '"') {
            return texto;
        }

        if (caractere == '\\') {
            if (!arquivo.get(caractere) ||
                (caractere != '"' && caractere != '\\' && caractere != '/')) {
                throw runtime_error("Escape nao suportado em banco.json.");
            }
        }

        if (static_cast<unsigned char>(caractere) < 32) {
            throw runtime_error("Caractere de controle em banco.json.");
        }

        texto += caractere;
    }

    throw runtime_error("Texto sem aspas finais em banco.json.");
}

void esperarCampo(istream& arquivo, const string& campo) {
    if (lerTexto(arquivo) != campo) {
        throw runtime_error("Campo ou ordem de campos invalida em banco.json.");
    }

    esperar(arquivo, ':');
}

int lerId(istream& arquivo) {
    arquivo >> ws;
    string texto;

    while (arquivo.peek() >= '0' && arquivo.peek() <= '9') {
        texto += static_cast<char>(arquivo.get());
    }

    int id = 0;
    const auto conversao = from_chars(texto.data(), texto.data() + texto.size(), id);

    if (texto.empty() || texto.front() == '0' || conversao.ec != errc{} || id <= 0) {
        throw runtime_error("ID invalido em banco.json.");
    }

    return id;
}
} // namespace

Requisicao interpretar(const string& comando) {
    istringstream entrada(comando);
    Requisicao requisicao;
    string idTexto;
    entrada >> requisicao.operacao >> idTexto;
    const auto conversao =
        from_chars(idTexto.data(), idTexto.data() + idTexto.size(), requisicao.id);

    if (conversao.ec != errc{} || conversao.ptr != idTexto.data() + idTexto.size() ||
        requisicao.id <= 0) {
        throw runtime_error("Use ID inteiro positivo. Exemplo: SELECT 1");
    }

    getline(entrada >> ws, requisicao.nome);
    const bool alteraNome =
        requisicao.operacao == "INSERT" || requisicao.operacao == "UPDATE";

    if (!alteraNome && requisicao.operacao != "SELECT" &&
        requisicao.operacao != "DELETE") {
        throw runtime_error("Operacoes: INSERT, SELECT, UPDATE e DELETE.");
    }

    if (alteraNome && !textoValido(requisicao.nome, 120)) {
        throw runtime_error("Nome deve ter 1 a 120 bytes UTF-8, sem controles.");
    }

    if (!alteraNome && !requisicao.nome.empty()) {
        throw runtime_error("SELECT e DELETE recebem somente ID.");
    }

    return requisicao;
}

Tabela::Tabela() {
    if (!fs::exists(caminho_)) {
        return;
    }

    if (fs::file_size(caminho_) > 1024 * 1024) {
        throw runtime_error("JSON excede 1 MiB.");
    }

    ifstream arquivo(caminho_, ios::binary);

    if (!arquivo) {
        throw runtime_error("Nao foi possivel ler os dados.");
    }

    esperar(arquivo, '{');
    esperarCampo(arquivo, "registros");
    esperar(arquivo, '[');
    arquivo >> ws;

    if (arquivo.peek() != ']') {
        while (true) {
            esperar(arquivo, '{');
            esperarCampo(arquivo, "id");
            const int id = lerId(arquivo);
            esperar(arquivo, ',');
            esperarCampo(arquivo, "nome");
            const string nome = lerTexto(arquivo);
            esperar(arquivo, '}');

            if (!textoValido(nome, 120) || buscar(id) != registros_.end() ||
                registros_.size() >= CAPACIDADE) {
                throw runtime_error("Nome invalido, ID duplicado ou tabela cheia.");
            }

            registros_.push_back({id, nome});

            arquivo >> ws;

            if (arquivo.peek() == ']') {
                break;
            }

            esperar(arquivo, ',');
        }
    }

    esperar(arquivo, ']');
    esperar(arquivo, '}');
    arquivo >> ws;

    if (!arquivo.eof()) {
        throw runtime_error("Conteudo extra apos o banco JSON.");
    }
}

vector<Registro>::iterator Tabela::buscar(int id) {
    return find_if(
        registros_.begin(), registros_.end(), [id](const Registro& registro) {
            return registro.id == id;
        });
}

// O semaforo permanece adquirido durante o CRUD e a persistencia.
string Tabela::executar(const Requisicao& requisicao) {
    const auto registro = buscar(requisicao.id);
    const bool existe = registro != registros_.end();

    if (requisicao.operacao == "INSERT" && existe) {
        return "ERRO: ID duplicado.";
    }

    if (requisicao.operacao != "INSERT" && !existe) {
        return "ERRO: Registro nao encontrado.";
    }

    if (requisicao.operacao == "SELECT") {
        return "OK: ID " + to_string(registro->id) + " | Nome: " + registro->nome;
    }

    if (requisicao.operacao == "INSERT" && registros_.size() >= CAPACIDADE) {
        return "ERRO: Tabela cheia.";
    }

    // Copia pequena: se salvar falhar, o estado original continua intacto.
    auto novosRegistros = registros_;

    if (requisicao.operacao == "DELETE") {
        novosRegistros.erase(novosRegistros.begin() + (registro - registros_.begin()));
    } else if (requisicao.operacao == "INSERT") {
        novosRegistros.push_back({requisicao.id, requisicao.nome});
    } else {
        novosRegistros[registro - registros_.begin()].nome = requisicao.nome;
    }

    salvar(novosRegistros);
    registros_.swap(novosRegistros);
    return "OK: " + requisicao.operacao + " concluido; banco salvo.";
}

void Tabela::salvar(const vector<Registro>& registros) {
    auto temporario = caminho_;
    temporario += L".tmp";
    ofstream arquivo(temporario, ios::binary | ios::trunc);

    if (!arquivo) {
        throw runtime_error("Nao foi possivel criar o JSON temporario.");
    }

    arquivo << "{\n  \"registros\": [";
    bool primeiro = true;

    for (const auto& [id, nome] : registros) {
        arquivo << (primeiro ? "\n" : ",\n");
        arquivo << "    {\"id\": " << id << ", \"nome\": ";
        // Nomes validos nao contem controles; quoted escapa aspas e barras.
        arquivo << quoted(nome) << '}';
        primeiro = false;
    }

    arquivo << (registros.empty() ? "" : "\n  ") << "]\n}\n";
    arquivo.close();

    if (!arquivo) {
        throw runtime_error("Falha ao gravar o JSON temporario.");
    }

    if (!MoveFileExW(temporario.c_str(),
            caminho_.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw runtime_error(ipc::erroWindows("Substituir JSON"));
    }
}

#pragma once
#include <filesystem>
#include <string>
#include <vector>

using namespace std;

struct Registro {
    int id;
    string nome;
};

struct Requisicao {
    string operacao;
    int id = 0;
    string nome;
};

Requisicao interpretar(const string& comando);

// O servidor adquire o semaforo antes de acessar a tabela.
class Tabela {
public:
    Tabela();
    string executar(const Requisicao& requisicao);

private:
    static constexpr size_t CAPACIDADE = 1000;
    vector<Registro> registros_;
    const filesystem::path caminho_ = "banco.json";

    vector<Registro>::iterator buscar(int id);
    void salvar(const vector<Registro>& registros);
};

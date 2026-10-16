// tmux-tree: explorador de arquivos em arvore no terminal (estilo neo-tree).
//
// Uso:  tmux-tree [pasta]
//   - sem argumento, comeca na pasta atual.
//   - Enter/clique numa pasta abre ou fecha; num arquivo imprime o caminho
//     absoluto no stdout e sai com 0.
//   - q / Esc sai sem imprimir nada (codigo 1).
//
// O programa nao sabe nada de tmux nem de nvim: ele so devolve o caminho.

#include <ncurses.h>

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <clocale>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Node {
    std::string name;   // nome exibido (com "/" no fim se for pasta)
    fs::path path;      // caminho absoluto
    bool is_dir = false;
    bool is_link = false;
    bool is_exec = false;
    bool expanded = false;
    bool loaded = false;                        // filhos ja foram lidos do disco?
    std::vector<std::unique_ptr<Node>> children;
};

struct Row {
    Node* node;
    int depth;
};

static bool eh_oculto(const std::string& nome) {
    return !nome.empty() && nome[0] == '.';
}

static void ordenar(Node& n) {
    std::sort(n.children.begin(), n.children.end(), [](const auto& a, const auto& b) {
        if (a->is_dir != b->is_dir) return a->is_dir;  // pastas primeiro
        return a->name < b->name;
    });
}

static void carregar(Node& n, bool mostrar_ocultos) {
    n.children.clear();
    std::error_code ec;
    fs::directory_iterator it(n.path, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator fim;
    while (!ec && it != fim) {
        const fs::directory_entry& e = *it;
        std::string nome = e.path().filename().string();
        if (mostrar_ocultos || !eh_oculto(nome)) {
            auto filho = std::make_unique<Node>();
            filho->path = e.path();
            std::error_code ec2;
            filho->is_dir = e.is_directory(ec2);
            filho->is_link = e.is_symlink();
            if (!filho->is_dir && !filho->is_link)
                filho->is_exec = (::access(filho->path.c_str(), X_OK) == 0);
            filho->name = filho->is_dir ? nome + "/" : nome;
            n.children.push_back(std::move(filho));
        }
        it.increment(ec);
    }
    ordenar(n);
    n.loaded = true;
}

static void garantir_carregado(Node& n, bool mostrar_ocultos) {
    if (n.is_dir && !n.loaded) carregar(n, mostrar_ocultos);
}

// Achata a arvore nas linhas visiveis (so desce em pastas expandidas).
static void coletar(const Node& n, int profundidade, std::vector<Row>& saida) {
    for (const auto& c : n.children) {
        saida.push_back({c.get(), profundidade});
        if (c->is_dir && c->expanded) coletar(*c, profundidade + 1, saida);
    }
}

// ---- icones (Nerd Font) e cores ----
// As cores usam os indices da paleta do terminal (COLOR_*), entao saem com as
// cores que o seu tema ja define. use_default_colors() mantem o fundo do tema.

static const char* ICONE_ARQUIVO = "\uf15b";  // fa-file
static const char* ICONE_PASTA = "\uf07b";    // fa-folder

struct IconeExt {
    const char* ext;
    const char* icone;
};

static const IconeExt ICONES[] = {
    {".c", "\ue61e"},    {".h", "\ue61e"},    {".cpp", "\ue61d"},  {".hpp", "\ue61d"},
    {".cc", "\ue61d"},   {".py", "\ue606"},   {".lua", "\ue620"},  {".js", "\ue60c"},
    {".ts", "\ue628"},   {".rs", "\ue7a8"},   {".go", "\ue627"},   {".sh", "\uf489"},
    {".md", "\uf48a"},   {".json", "\ue60b"}, {".toml", "\ue615"}, {".yml", "\ue615"},
    {".yaml", "\ue615"}, {".txt", "\uf15c"},  {".png", "\uf1c5"},  {".jpg", "\uf1c5"},
    {".jpeg", "\uf1c5"}, {".gif", "\uf1c5"},  {".svg", "\uf1c5"},  {".mp4", "\uf03d"},
    {".mp3", "\uf001"},  {".zip", "\uf1c6"},  {".pdf", "\uf1c1"},
};

static std::string minusculo(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static const char* icone_para(const Node& n) {
    if (n.is_dir) return ICONE_PASTA;
    std::string ext = minusculo(n.path.extension().string());
    for (const IconeExt& t : ICONES)
        if (ext == t.ext) return t.icone;
    return ICONE_ARQUIVO;
}

enum { CP_PASTA = 1, CP_EXEC, CP_LINK, CP_MIDIA, CP_CODIGO };

static int cor_para(const Node& n) {
    if (n.is_dir) return CP_PASTA;
    if (n.is_link) return CP_LINK;
    if (n.is_exec) return CP_EXEC;
    std::string ext = minusculo(n.path.extension().string());
    static const char* MIDIA[] = {".png", ".jpg", ".jpeg", ".gif",
                                  ".svg", ".webp", ".mp4", ".mp3"};
    for (const char* m : MIDIA)
        if (ext == m) return CP_MIDIA;
    static const char* CODIGO[] = {".c",   ".h",    ".cpp",  ".hpp", ".cc",  ".py",
                                   ".lua", ".js",   ".ts",   ".rs",  ".go",  ".sh",
                                   ".json", ".toml", ".yml", ".yaml"};
    for (const char* c : CODIGO)
        if (ext == c) return CP_CODIGO;
    return 0;  // sem par de cor: fica com o padrao do terminal
}

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");

    fs::path inicio;
    try {
        inicio = (argc > 1) ? fs::absolute(fs::path(argv[1])) : fs::current_path();
    } catch (const fs::filesystem_error& e) {
        std::fprintf(stderr, "erro de caminho: %s\n", e.what());
        return 1;
    }

    std::error_code ec;
    if (!fs::is_directory(inicio, ec)) {
        std::fprintf(stderr, "nao e uma pasta: %s\n", inicio.c_str());
        return 1;
    }

    bool mostrar_ocultos = false;

    auto criar_raiz = [&]() {
        auto raiz = std::make_unique<Node>();
        raiz->path = inicio;
        raiz->is_dir = true;
        raiz->expanded = true;
        carregar(*raiz, mostrar_ocultos);  // ja deixa loaded = true
        return raiz;
    };

    auto raiz = criar_raiz();

    // O desenho vai para /dev/tty e o caminho escolhido sai no stdout. Assim
    // quem chamar com "$(tmux-tree)" nao engole a interface (o ncurses escreve
    // no stdout, que estaria capturado pelo pipe, e a tela ficaria em branco).
    FILE* tty_out = std::fopen("/dev/tty", "w");
    FILE* tty_in = std::fopen("/dev/tty", "r");
    SCREEN* scr = (tty_out && tty_in) ? newterm(nullptr, tty_out, tty_in) : nullptr;
    if (!scr) {
        std::fprintf(stderr, "nao consegui abrir o terminal (/dev/tty)\n");
        return 1;
    }
    set_escdelay(25);
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);
    mouseinterval(0);              // clique responde na hora (PRESSED/RELEASED)
    mousemask(ALL_MOUSE_EVENTS, nullptr);

    if (has_colors()) {
        start_color();
        use_default_colors();  // fundo igual ao do terminal
        init_pair(CP_PASTA, COLOR_BLUE, -1);
        init_pair(CP_EXEC, COLOR_GREEN, -1);
        init_pair(CP_LINK, COLOR_CYAN, -1);
        init_pair(CP_MIDIA, COLOR_MAGENTA, -1);
        init_pair(CP_CODIGO, COLOR_YELLOW, -1);
    }

    std::vector<Row> linhas;
    int cursor = 0;
    int topo = 0;
    bool rodando = true;
    int retorno = 1;
    fs::path escolhido;

    while (rodando) {
        linhas.clear();
        coletar(*raiz, 0, linhas);

        if (linhas.empty()) cursor = 0;
        else if (cursor >= (int)linhas.size()) cursor = (int)linhas.size() - 1;
        if (cursor < 0) cursor = 0;

        int altura = LINES - 1;  // ultima linha fica pro rodape
        if (altura < 1) altura = 1;
        if (cursor < topo) topo = cursor;
        if (cursor >= topo + altura) topo = cursor - altura + 1;
        if (topo < 0) topo = 0;

        erase();
        for (int i = 0; i < altura; ++i) {
            int idx = topo + i;
            if (idx >= (int)linhas.size()) break;
            const Row& r = linhas[idx];

            std::string texto(r.depth * 2, ' ');
            texto += r.node->is_dir ? (r.node->expanded ? "\u25be " : "\u25b8 ") : "  ";
            texto += icone_para(*r.node);
            texto += ' ';
            texto += r.node->name;

            int par = cor_para(*r.node);
            if (idx == cursor) attron(A_REVERSE);
            if (par) attron(COLOR_PAIR(par));
            if (r.node->is_dir) attron(A_BOLD);
            mvaddstr(i, 0, texto.c_str());
            if (r.node->is_dir) attroff(A_BOLD);
            if (par) attroff(COLOR_PAIR(par));
            if (idx == cursor) attroff(A_REVERSE);
        }

        std::string ajuda = " " + inicio.string() +
                            "   j/k mover   Enter/clique abre-fecha   . ocultos   q sair";
        attron(A_REVERSE);
        mvaddstr(LINES - 1, 0, ajuda.c_str());
        attroff(A_REVERSE);
        refresh();

        int tecla = getch();
        switch (tecla) {
            case 'q':
            case 27:  // Esc
                rodando = false;
                break;

            case 'j':
            case KEY_DOWN:
                ++cursor;
                break;

            case 'k':
            case KEY_UP:
                --cursor;
                break;

            case 'g':
                cursor = 0;
                break;

            case 'G':
                cursor = (int)linhas.size() - 1;
                break;

            case '\n':
            case '\r':
            case KEY_ENTER:
            case 'l':
            case KEY_RIGHT: {
                if (linhas.empty()) break;
                Node& n = *linhas[cursor].node;
                if (n.is_dir) {
                    garantir_carregado(n, mostrar_ocultos);
                    n.expanded = !n.expanded;
                } else {
                    escolhido = n.path;
                    retorno = 0;
                    rodando = false;
                }
                break;
            }

            case 'h':
            case KEY_LEFT: {
                if (linhas.empty()) break;
                Node& n = *linhas[cursor].node;
                if (n.is_dir && n.expanded) n.expanded = false;
                break;
            }

            case '.': {
                mostrar_ocultos = !mostrar_ocultos;
                raiz = criar_raiz();  // recomeca (perde o que estava aberto)
                cursor = 0;
                topo = 0;
                break;
            }

            case KEY_MOUSE: {
                MEVENT ev;
                if (getmouse(&ev) != OK) break;

                if (ev.bstate & BUTTON4_PRESSED) {         // roda do mouse pra cima
                    --cursor;
                } else if (ev.bstate & BUTTON5_PRESSED) {  // roda pra baixo
                    ++cursor;
                } else if (ev.bstate & BUTTON1_RELEASED) {  // clique esquerdo
                    int idx = topo + ev.y;
                    if (ev.y >= 0 && ev.y < altura && idx < (int)linhas.size()) {
                        cursor = idx;
                        Node& n = *linhas[cursor].node;
                        if (n.is_dir) {
                            garantir_carregado(n, mostrar_ocultos);
                            n.expanded = !n.expanded;
                        } else {
                            escolhido = n.path;
                            retorno = 0;
                            rodando = false;
                        }
                    }
                }
                break;
            }

            default:
                break;
        }
    }

    endwin();
    delscreen(scr);
    std::fclose(tty_out);
    std::fclose(tty_in);

    if (retorno == 0) std::printf("%s\n", escolhido.c_str());
    return retorno;
}

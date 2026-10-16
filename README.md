# tmux-tree

Explorador de arquivos em árvore para o terminal, no estilo *neo-tree* (a
sidebar do VS Code). Feito para rodar num painel do tmux, mas não depende dele.

O programa **só lista e desenha**. Ao escolher um arquivo, ele imprime o caminho
absoluto no stdout e sai — quem decide o que fazer com o caminho é o shell.

```
▾  src/
    ▾  nested/
        deep.txt
     app.cpp
     util.h
    README.md
    main.cpp
```

## Requisitos

- Compilador C++20 e `ncursesw`
- Uma [Nerd Font](https://www.nerdfonts.com/) para os ícones aparecerem

## Compilar

```sh
make
```

## Uso

```sh
tmux-tree [pasta]      # sem argumento, começa na pasta atual
```

- Escolher um arquivo: imprime o caminho absoluto e sai com código `0`.
- `q` / `Esc`: sai sem imprimir nada, com código `1`.

O desenho vai para `/dev/tty`, então o caminho escolhido pode ser capturado com
`$( )`, do mesmo jeito que o `fzf` faz:

```sh
nvim "$(tmux-tree ~/projeto)"
```

## Teclas

| tecla | ação |
| --- | --- |
| `j` / `k` / setas | mover |
| `Enter` / `→` / clique | abre ou fecha pasta; num arquivo, escolhe |
| `←` | recolhe a pasta |
| `.` | mostra / esconde ocultos |
| roda do mouse | rola |
| `g` / `G` | topo / fim |
| `q` / `Esc` | sair |

## Cores

Usa a paleta do próprio terminal (`COLOR_*` do ncurses), com o fundo do tema:
pastas em azul, código em amarelo, executáveis em verde, links em ciano e mídia
em magenta.

## No tmux

Exemplo de bind que abre o picker à esquerda e manda o arquivo escolhido para o
painel da direita:

```tmux
bind f split-window -h -b -l 30% -c "#{pane_current_path}" "$HOME/caminho/para/abrir-ao-lado.sh"
```

O `abrir-ao-lado.sh` detecta o painel da direita: se já roda o nvim, manda
`:edit <arquivo>`; se for um shell, abre o nvim ali mesmo.

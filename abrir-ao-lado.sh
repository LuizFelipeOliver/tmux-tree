#!/bin/sh
# Abre o picker tmux-tree e manda o arquivo escolhido pro painel a direita.
#   - se no painel a direita ja roda o nvim: manda ":edit <arquivo>";
#   - se for um shell: abre o nvim ali mesmo com o arquivo.
# Se nada for escolhido, nao faz nada.

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

f="$("$dir/tmux-tree" "$PWD")" || exit 0
[ -n "$f" ] || exit 0

alvo='{right-of}'
cmd="$(tmux display-message -p -t "$alvo" '#{pane_current_command}' 2>/dev/null)"

case "$cmd" in
    nvim|vim|vi|nvim-*)
        tmux send-keys -t "$alvo" ":edit $f" Enter
        ;;
    *)
        tmux send-keys -t "$alvo" "nvim $(printf '%q' "$f")" Enter
        ;;
esac

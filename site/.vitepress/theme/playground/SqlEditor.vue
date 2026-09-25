<script setup lang="ts">
// CodeMirror 6 SQL editor, themed with VitePress CSS variables so it follows
// the site's light/dark mode automatically (an isDark watcher swaps the
// base-theme compartment so CM's own defaults match too).
import { onBeforeUnmount, onMounted, ref, watch } from "vue";
import { EditorView, keymap, placeholder } from "@codemirror/view";
import { Compartment, Prec } from "@codemirror/state";
import { basicSetup } from "codemirror";
import { sql, SQLite } from "@codemirror/lang-sql";
import { HighlightStyle, syntaxHighlighting } from "@codemirror/language";
import { tags } from "@lezer/highlight";
import { useData } from "vitepress";

const props = defineProps<{ modelValue: string }>();
const emit = defineEmits<{
  (e: "update:modelValue", value: string): void;
  (e: "run"): void;
}>();

const { isDark } = useData();
const container = ref<HTMLElement | null>(null);
let view: EditorView | null = null;
const darkCompartment = new Compartment();

// Syntax colors via VitePress palette variables — valid in both themes.
const highlight = HighlightStyle.define([
  { tag: tags.keyword, color: "var(--vp-c-brand-1)", fontWeight: "600" },
  { tag: tags.string, color: "var(--vp-c-green-1)" },
  { tag: tags.number, color: "var(--vp-c-purple-1)" },
  { tag: tags.bool, color: "var(--vp-c-purple-1)" },
  { tag: tags.null, color: "var(--vp-c-purple-1)" },
  { tag: tags.comment, color: "var(--vp-c-text-3)", fontStyle: "italic" },
  { tag: tags.operator, color: "var(--vp-c-text-2)" },
  { tag: tags.punctuation, color: "var(--vp-c-text-2)" },
  { tag: tags.typeName, color: "var(--vp-c-yellow-1)" },
]);

const cmTheme = EditorView.theme({
  "&": {
    backgroundColor: "var(--vp-c-bg-soft)",
    color: "var(--vp-c-text-1)",
    border: "1px solid var(--vp-c-divider)",
    borderRadius: "8px",
    fontSize: "14px",
  },
  "&.cm-focused": {
    outline: "none",
    borderColor: "var(--vp-c-brand-1)",
  },
  ".cm-scroller": {
    fontFamily: "var(--vp-font-family-mono)",
    lineHeight: "1.6",
    minHeight: "10rem",
    maxHeight: "24rem",
    borderRadius: "8px",
  },
  ".cm-content": { caretColor: "var(--vp-c-text-1)", padding: "8px 0" },
  ".cm-cursor": { borderLeftColor: "var(--vp-c-text-1)" },
  ".cm-gutters": {
    backgroundColor: "transparent",
    color: "var(--vp-c-text-3)",
    border: "none",
  },
  ".cm-activeLine": { backgroundColor: "transparent" },
  ".cm-activeLineGutter": {
    backgroundColor: "transparent",
    color: "var(--vp-c-text-2)",
  },
  "&.cm-focused .cm-activeLine": {
    backgroundColor: "var(--vp-c-default-soft)",
  },
  ".cm-selectionBackground, &.cm-focused .cm-selectionBackground": {
    backgroundColor: "var(--vp-c-brand-soft) !important",
  },
  ".cm-tooltip": {
    backgroundColor: "var(--vp-c-bg-elv)",
    color: "var(--vp-c-text-1)",
    border: "1px solid var(--vp-c-divider)",
  },
  ".cm-tooltip-autocomplete ul li[aria-selected]": {
    backgroundColor: "var(--vp-c-brand-soft)",
    color: "var(--vp-c-text-1)",
  },
});

onMounted(() => {
  view = new EditorView({
    parent: container.value!,
    doc: props.modelValue,
    extensions: [
      Prec.highest(
        keymap.of([
          {
            key: "Mod-Enter",
            run: () => {
              emit("run");
              return true;
            },
          },
        ])
      ),
      basicSetup,
      sql({ dialect: SQLite, upperCaseKeywords: false }),
      syntaxHighlighting(highlight),
      cmTheme,
      darkCompartment.of(EditorView.theme({}, { dark: isDark.value })),
      placeholder("select tg_version();"),
      EditorView.updateListener.of((update) => {
        if (update.docChanged) {
          emit("update:modelValue", update.state.doc.toString());
        }
      }),
    ],
  });
});

watch(isDark, (dark) => {
  view?.dispatch({
    effects: darkCompartment.reconfigure(EditorView.theme({}, { dark })),
  });
});

// External updates (dataset example SQL, ?sql= param) flow back into the doc.
watch(
  () => props.modelValue,
  (value) => {
    if (view && value !== view.state.doc.toString()) {
      view.dispatch({
        changes: { from: 0, to: view.state.doc.length, insert: value },
      });
    }
  }
);

onBeforeUnmount(() => {
  view?.destroy();
  view = null;
});
</script>

<template>
  <div ref="container" class="pg-editor" aria-label="SQL editor"></div>
</template>

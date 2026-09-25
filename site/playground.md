---
title: Playground
layout: page
sidebar: false
---

<script setup>
import { defineAsyncComponent } from "vue";

// Client-only: the playground (and the sqlite3 WASM module it loads) must
// never be evaluated during SSR/`vitepress build`.
const PlaygroundPage = defineAsyncComponent(
  () => import("./.vitepress/theme/playground/PlaygroundPage.vue")
);
</script>

<ClientOnly>
  <PlaygroundPage />
</ClientOnly>

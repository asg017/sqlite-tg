# The `datasette-sqlite-tg` Datasette Plugin

`datasette-sqlite-tg` is a [Datasette plugin](https://docs.datasette.io/en/stable/plugins.html) that loads the [`sqlite-tg`](https://github.com/asg017/sqlite-tg) extension in Datasette instances.

```
datasette install datasette-sqlite-tg
```

See [`docs.md`](../../docs.md) for a full API reference for the tg SQL functions.

Alternatively, when publishing Datasette instances, you can use the `--install` option to install the plugin.

```
datasette publish cloudrun data.db --service=my-service --install=datasette-sqlite-tg

```

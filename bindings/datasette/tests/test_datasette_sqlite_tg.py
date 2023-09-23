from datasette.app import Datasette
import pytest


@pytest.mark.asyncio
async def test_plugin_is_installed():
    datasette = Datasette(memory=True)
    response = await datasette.client.get("/-/plugins.json")
    assert response.status_code == 200
    installed_plugins = {p["name"] for p in response.json()}
    assert "datasette-sqlite-tg" in installed_plugins

@pytest.mark.asyncio
async def test_sqlite_tg_functions():
    datasette = Datasette(memory=True)
    response = await datasette.client.get("/_memory.json?sql=select+tg_version(),tg('alex')")
    assert response.status_code == 200
    tg_version, tg = response.json()["rows"][0]
    assert tg_version[0] == "v"
    assert len(tg) == 26

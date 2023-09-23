# sqlite-tg

Work-in-progress geospatial SQLite extension around [tg](https://github.com/tidwall/tg). Not ready yet, but feel free to play with it!

Once stabilized, will be a part of [sqlite-ecosystem](https://github.com/asg017/sqlite-ecosystem).

-
```sql
select tg_point_geojson(x, y);
select tg_point_wkt(x, y);
select tg_point_wkb(x, y);

select tg_line_XXX(p1, p2, ...);
select tg_group_line_XXX(pp);

tg_polygon_XXX()
```

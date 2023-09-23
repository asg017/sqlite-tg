.load dist/tg0

select tg_version();

select tg_intersects('LINESTRING (0 0, 2 2)', 'LINESTRING (1 0, 1 2)');
select tg_intersects('LINESTRING (0 0, 0 2)', 'LINESTRING (2 0, 2 2)');

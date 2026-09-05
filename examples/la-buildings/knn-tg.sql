.load ../../dist/tg0
.timer on

-- the 10 buildings nearest a point in Hollywood (distance in meters to the
-- nearest edge of each footprint; 0 for the building the point is inside)
select rowid, round(distance, 1) as meters
from tg_buildings
where boundary match tg_point(-118.3372, 34.1199)
order by distance
limit 10;

-- every building within 250 m, nearest first
select count(*)
from tg_buildings
where boundary match tg_point(-118.3372, 34.1199) and distance <= 250
order by distance;

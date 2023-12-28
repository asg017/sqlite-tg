.load ../../dist/tg0
.timer on

.param set :query '{"type":"Feature","properties":{},"geometry":{"type":"Polygon","coordinates":[[[-118.3372167,34.1199871],[-118.3474114,34.1127637],[-118.3335266,34.105954],[-118.3335266,34.105954],[-118.3372167,34.1199871]]]}}'

select count(*)
from rtree_buildings, tg_bbox(:query)
where rtree_buildings.minX >= tg_bbox.minX and rtree_buildings.maxX <= tg_bbox.maxX
  and rtree_buildings.minY >= tg_bbox.minY and rtree_buildings.maxY <= tg_bbox.maxY
  and tg_intersects(rtree_buildings.boundary, :query)

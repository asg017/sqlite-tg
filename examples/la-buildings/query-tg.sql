.load ../../dist/tg0
.timer on

.param set :query '{"type":"Feature","properties":{},"geometry":{"type":"Polygon","coordinates":[[[-118.3372167,34.1199871],[-118.3474114,34.1127637],[-118.3335266,34.105954],[-118.3335266,34.105954],[-118.3372167,34.1199871]]]}}'
select count(*) from tg_buildings where tg_intersects(_shape, :query);

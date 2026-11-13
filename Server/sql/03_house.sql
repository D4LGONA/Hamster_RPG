-- 2026-11-13: 집 / 가구 저장 프로시저. 01_schema.sql(테이블) 다음에 실행. 여러 번 실행해도 안전.
--
-- id는 서버가 정해서 넣는다. 서버 메모리 id = 대역(집 1,000,000 / 가구 3,000,000) + DB id.
-- 서버가 시작할 때 sp_world_load로 전부 읽고, 가장 큰 id 다음부터 새 id를 준다.
-- (AUTO_INCREMENT 열이지만 값을 직접 넣으면 그 값이 들어간다)

USE hamster_rpg;

-- 서버 시작 때 한 번. 결과 1 = 집 전부, 결과 2 = 가구 전부
DROP PROCEDURE IF EXISTS sp_world_load;
DELIMITER //
CREATE PROCEDURE sp_world_load()
BEGIN
    SELECT house_id, owner_name, world_x, world_y FROM house ORDER BY house_id;
    SELECT furniture_id, house_id, item_type, x, y, rotation FROM furniture ORDER BY furniture_id;
END //
DELIMITER ;

DROP PROCEDURE IF EXISTS sp_house_create;
DELIMITER //
CREATE PROCEDURE sp_house_create(IN p_house_id BIGINT UNSIGNED, IN p_owner VARCHAR(20), IN p_x FLOAT, IN p_y FLOAT)
BEGIN
    INSERT INTO house (house_id, owner_name, world_x, world_y) VALUES (p_house_id, p_owner, p_x, p_y);
END //
DELIMITER ;

-- 설치와 이동을 하나로: 없으면 넣고 있으면 고친다
DROP PROCEDURE IF EXISTS sp_furniture_save;
DELIMITER //
CREATE PROCEDURE sp_furniture_save(
    IN p_furniture_id BIGINT UNSIGNED,
    IN p_house_id     BIGINT UNSIGNED,
    IN p_item_type    INT UNSIGNED,
    IN p_x            FLOAT,
    IN p_y            FLOAT,
    IN p_rotation     TINYINT UNSIGNED)
BEGIN
    INSERT INTO furniture (furniture_id, house_id, item_type, x, y, rotation)
    VALUES (p_furniture_id, p_house_id, p_item_type, p_x, p_y, p_rotation)
    ON DUPLICATE KEY UPDATE x = VALUES(x), y = VALUES(y), rotation = VALUES(rotation);
END //
DELIMITER ;

DROP PROCEDURE IF EXISTS sp_furniture_delete;
DELIMITER //
CREATE PROCEDURE sp_furniture_delete(IN p_furniture_id BIGINT UNSIGNED)
BEGIN
    DELETE FROM furniture WHERE furniture_id = p_furniture_id;
END //
DELIMITER ;

-- 확인용
-- CALL sp_house_create(1, 'test', 10, 20);
-- CALL sp_furniture_save(1, 1, 0, 1.0, 2.0, 1);
-- CALL sp_world_load();
SHOW TABLES;

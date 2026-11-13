-- 2026-11-12: 캐릭터 저장 (DB 1단계).
-- 2026-11-13: 인벤토리(20칸)와 무기 장착 칸 추가. 무기 강화 수치(enhance) 추가.
--
-- 실행 방법: 01_schema.sql 다음에 MySQL Workbench에서 이 파일을 열고 번개(⚡) 버튼으로 전체 실행.
-- 여러 번 실행해도 안전하다(테이블은 IF NOT EXISTS, 프로시저는 지우고 다시 만든다).
--
-- 무엇을 저장하나: 원본 값만. level / exp / gold / hp / 위치.
--   maxHp, attack, defense, combatPower, requiredExp 는 서버가 레벨로 계산한다(PlayerStats.h).
--   DB에도 두면 공식을 바꿨을 때 옛 값과 새 공식이 어긋난다. 가구 크기를 안 넣은 것과 같은 이유.
--
-- 누구의 캐릭터인가: 지금 로그인은 이름만 받는다. 그래서 이름이 곧 열쇠다(house.owner_name과 같은 규칙).
-- 계정/비밀번호가 생기면 account 테이블을 만들고 account_id로 묶는다.
--
-- 서버는 SQL 문장을 직접 만들지 않고 아래 프로시저만 부른다.
-- 나중에 C# 서버도 같은 프로시저를 부르면, 두 서버의 차이는 DB 로직이 아니라 서버 구조에만 남는다.

USE hamster_rpg;

-- ---------------------------------------------------------------
-- player_character  (CHARACTER는 MySQL 예약어라 이름을 피했다)
-- ---------------------------------------------------------------
CREATE TABLE IF NOT EXISTS player_character
(
    character_id  BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    name          VARCHAR(20)     NOT NULL,                     -- protocol.h의 NAME_SIZE와 맞춤
    level         INT             NOT NULL DEFAULT 1,
    exp           INT             NOT NULL DEFAULT 0,
    gold          INT             NOT NULL DEFAULT 0,
    hp            INT             NOT NULL DEFAULT 100,
    pos_x         FLOAT           NOT NULL DEFAULT 0,          -- 월드 좌표. 집 안에서 나갔으면 집 문 옆 좌표
    pos_y         FLOAT           NOT NULL DEFAULT 0,
    weapon_slot   TINYINT         NOT NULL DEFAULT -1,         -- 장착한 무기가 든 인벤토리 칸. -1이면 맨손
    created_at    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    last_login_at DATETIME        NULL,

    PRIMARY KEY (character_id),
    UNIQUE KEY uq_character_name (name),

    CONSTRAINT chk_character_level CHECK (level >= 1),
    CONSTRAINT chk_character_gold  CHECK (gold >= 0)
) ENGINE = InnoDB;

-- 이미 2026-11-12 버전으로 테이블을 만들었으면 weapon_slot 열을 붙인다(MySQL은 ADD COLUMN IF NOT EXISTS가 없어서 직접 확인).
DROP PROCEDURE IF EXISTS sp_migrate_weapon_slot;
DELIMITER //
CREATE PROCEDURE sp_migrate_weapon_slot()
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'player_character' AND COLUMN_NAME = 'weapon_slot') THEN
        ALTER TABLE player_character ADD COLUMN weapon_slot TINYINT NOT NULL DEFAULT -1 AFTER pos_y;
    END IF;
END //
DELIMITER ;
CALL sp_migrate_weapon_slot();
DROP PROCEDURE sp_migrate_weapon_slot;

-- ---------------------------------------------------------------
-- player_inventory: 빈 칸은 줄이 없다. 칸 번호(0~19)가 곧 가방 위치.
-- 아이템 이름/효과는 서버 Item.h가 정답이고 여기는 id와 개수만 둔다.
-- ---------------------------------------------------------------
CREATE TABLE IF NOT EXISTS player_inventory
(
    name     VARCHAR(20)       NOT NULL,
    slot     TINYINT UNSIGNED  NOT NULL,
    item_id  SMALLINT UNSIGNED NOT NULL,
    count    SMALLINT UNSIGNED NOT NULL,
    enhance  TINYINT UNSIGNED  NOT NULL DEFAULT 0,             -- 무기 강화 수치(+0 ~ +10)

    PRIMARY KEY (name, slot),
    CONSTRAINT fk_inventory_character FOREIGN KEY (name) REFERENCES player_character (name) ON DELETE CASCADE,
    CONSTRAINT chk_inventory_slot  CHECK (slot < 20),
    CONSTRAINT chk_inventory_count CHECK (count > 0)
) ENGINE = InnoDB;

-- 강화 수치 열이 없는 예전 테이블이면 붙인다
DROP PROCEDURE IF EXISTS sp_migrate_enhance;
DELIMITER //
CREATE PROCEDURE sp_migrate_enhance()
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'player_inventory' AND COLUMN_NAME = 'enhance') THEN
        ALTER TABLE player_inventory ADD COLUMN enhance TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER count;
    END IF;
END //
DELIMITER ;
CALL sp_migrate_enhance();
DROP PROCEDURE sp_migrate_enhance;

-- ---------------------------------------------------------------
-- sp_character_load: 이름으로 불러오기. 없으면 새로 만들고 기본값을 돌려준다.
-- 결과 1: level, exp, gold, hp, pos_x, pos_y, is_new (1이면 방금 만든 캐릭터), weapon_slot
-- 결과 2: 인벤토리 (slot, item_id, count, enhance)
-- ---------------------------------------------------------------
DROP PROCEDURE IF EXISTS sp_character_load;
DELIMITER //
CREATE PROCEDURE sp_character_load(IN p_name VARCHAR(20))
BEGIN
    DECLARE v_is_new TINYINT DEFAULT 0;

    -- 동시에 같은 이름으로 두 번 들어와도 UNIQUE 덕에 한 줄만 생긴다.
    INSERT IGNORE INTO player_character (name) VALUES (p_name);
    IF ROW_COUNT() > 0 THEN
        SET v_is_new = 1;
    END IF;

    UPDATE player_character SET last_login_at = CURRENT_TIMESTAMP WHERE name = p_name;

    SELECT level, exp, gold, hp, pos_x, pos_y, v_is_new AS is_new, weapon_slot
      FROM player_character
     WHERE name = p_name;

    SELECT slot, item_id, count, enhance
      FROM player_inventory
     WHERE name = p_name
     ORDER BY slot;
END //
DELIMITER ;

-- ---------------------------------------------------------------
-- sp_character_save: 로그아웃할 때 + 서버가 주기적으로(자동 저장) 부른다.
-- ---------------------------------------------------------------
DROP PROCEDURE IF EXISTS sp_character_save;
DELIMITER //
CREATE PROCEDURE sp_character_save(
    IN p_name  VARCHAR(20),
    IN p_level INT,
    IN p_exp   INT,
    IN p_gold  INT,
    IN p_hp    INT,
    IN p_x     FLOAT,
    IN p_y     FLOAT,
    IN p_weapon_slot TINYINT)
BEGIN
    UPDATE player_character
       SET level = p_level, exp = p_exp, gold = p_gold, hp = p_hp, pos_x = p_x, pos_y = p_y, weapon_slot = p_weapon_slot
     WHERE name = p_name;

    -- 가방은 통째로 갈아끼운다. 서버는 이 뒤에 sp_inventory_set을 칸 수만큼 부른다(같은 트랜잭션).
    DELETE FROM player_inventory WHERE name = p_name;
END //
DELIMITER ;

DROP PROCEDURE IF EXISTS sp_inventory_set;
DELIMITER //
CREATE PROCEDURE sp_inventory_set(
    IN p_name    VARCHAR(20),
    IN p_slot    TINYINT UNSIGNED,
    IN p_item_id SMALLINT UNSIGNED,
    IN p_count   SMALLINT UNSIGNED,
    IN p_enhance TINYINT UNSIGNED)
BEGIN
    INSERT INTO player_inventory (name, slot, item_id, count, enhance) VALUES (p_name, p_slot, p_item_id, p_count, p_enhance);
END //
DELIMITER ;

-- ---------------------------------------------------------------
-- 확인용
-- ---------------------------------------------------------------
-- CALL sp_character_load('test');
-- CALL sp_character_save('test', 3, 40, 120, 90, 12.5, -3.0, 0);
-- CALL sp_inventory_set('test', 0, 101, 1, 3);
-- SELECT * FROM player_character;
SHOW TABLES;

-- 2026-09-25: Hamster_RPG 스키마 초안.
-- 설계 근거: Docs/FurnitureDesign.md 6장
--
-- 실행 방법: MySQL Workbench에서 이 파일을 열고 번개(⚡) 버튼으로 전체 실행.
-- 여러 번 실행해도 안전하다(IF NOT EXISTS).

CREATE DATABASE IF NOT EXISTS hamster_rpg
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE hamster_rpg;

-- ---------------------------------------------------------------
-- house: 플레이어가 월드에 설치한 집
-- ---------------------------------------------------------------
-- 지금 서버는 소유권을 '이름'으로 건다. DB가 없어서 playerId가 재접속마다 바뀌기 때문.
-- 계정 시스템이 생기면 owner_account_id를 채우고 owner_name은 표시용으로만 남긴다.
CREATE TABLE IF NOT EXISTS house
(
    house_id         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    owner_name       VARCHAR(20)     NOT NULL,                  -- protocol.h의 NAME_SIZE와 맞춤
    owner_account_id BIGINT UNSIGNED NULL,                      -- 계정 붙으면 사용
    world_x          FLOAT           NOT NULL,
    world_y          FLOAT           NOT NULL,
    created_at       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (house_id),

    -- 한 사람이 집 하나. 서버도 FindHouseByOwner로 같은 규칙을 지킨다.
    UNIQUE KEY uq_house_owner_name (owner_name),

    KEY idx_house_world_pos (world_x, world_y)                  -- 나중에 영역 조회용
) ENGINE = InnoDB;

-- ---------------------------------------------------------------
-- furniture: 집 안에 놓인 가구
-- ---------------------------------------------------------------
-- item_type: 0 침대 / 1 책상 / 2 의자 / 3 화분 / 4 러그 (Docs/FurnitureDesign.md 8장)
--   충돌 판정에 쓰는 가구 크기는 서버 코드(FURNITURE_SIZE)가 기준이다.
--   DB에 크기를 또 두면 둘이 어긋날 수 있어서 넣지 않았다.
-- rotation: 0=0도, 1=90도, 2=180도, 3=270도
CREATE TABLE IF NOT EXISTS furniture
(
    furniture_id BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
    house_id     BIGINT UNSIGNED  NOT NULL,
    item_type    INT UNSIGNED     NOT NULL,
    x            FLOAT            NOT NULL,                     -- 집 안 좌표 (±4.5)
    y            FLOAT            NOT NULL,
    rotation     TINYINT UNSIGNED NOT NULL DEFAULT 0,
    created_at   DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at   DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (furniture_id),

    -- 집에 들어갈 때 "이 집 가구 전부"를 조회하므로 반드시 필요한 인덱스
    KEY idx_furniture_house (house_id),

    CONSTRAINT fk_furniture_house
        FOREIGN KEY (house_id) REFERENCES house (house_id)
        ON DELETE CASCADE,                                      -- 집이 사라지면 가구도 같이

    CONSTRAINT chk_furniture_rotation CHECK (rotation BETWEEN 0 AND 3)
) ENGINE = InnoDB;

-- ---------------------------------------------------------------
-- 확인용 조회
-- ---------------------------------------------------------------
SHOW TABLES;

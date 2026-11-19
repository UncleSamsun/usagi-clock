# 순수 C 컴포넌트: 디바운스와 짧게/길게 판별만 한다. GPIO 연결은 계획 2.
# .c 파일을 추가하면 SRCS 목록에도 추가한다.
idf_component_register(
    SRCS "button_fsm.c"
    INCLUDE_DIRS "include"
)

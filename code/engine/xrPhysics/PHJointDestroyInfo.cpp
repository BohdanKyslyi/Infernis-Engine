#include "stdafx.h"
#include "PHJointDestroyInfo.h"
#include "PhysicsCommon.h"
#include "mathutilsode.h"
#include "console_vars.h"
#include <immintrin.h>

CPHJointDestroyInfo::CPHJointDestroyInfo(float break_force, float break_torque) 
    : m_sq_break_force(break_force * break_force), 
      m_sq_break_torque(break_torque * break_torque),
      m_breaked(false) 
{
    // Векторизоване обнулення структури dJointFeedback (64 байти)
    // dVector3 у ODE — це масив із 4-х dReal (де 4-й елемент - це padding). 
    // Загалом 4 вектори * 4 float * 4 байти = 64 байти (два регістри __m256).
    __m256 zero = _mm256_setzero_ps();
    
    _mm256_storeu_ps(reinterpret_cast<float*>(&m_joint_feedback.f1), zero);
    // f2 та t2 розміщені відразу після f1 та t1
    _mm256_storeu_ps(reinterpret_cast<float*>(&m_joint_feedback.f2), zero); 
}

bool CPHJointDestroyInfo::Update() {
    if (m_breaked) return true;

    const float force_threshold = m_sq_break_force / ph_console::phBreakCommonFactor;
    const float torque_threshold = m_sq_break_torque / ph_console::phBreakCommonFactor;

    // Структура dJointFeedback має такий порядок у пам'яті: [f1, t1, f2, t2]
    // Завантажуємо дані. v_f1_t1 міститиме f1 у нижній половині (128 біт) і t1 у верхній (128 біт).
    const float* fb_ptr = reinterpret_cast<const float*>(&m_joint_feedback);
    __m256 v_f1_t1 = _mm256_loadu_ps(fb_ptr);      
    __m256 v_f2_t2 = _mm256_loadu_ps(fb_ptr + 8);  

    // Обчислюємо квадрат довжини через Dot Product
    // Маска 0x7F (0111 1111): множимо перші 3 елементи (x, y, z), сумуємо і розмножуємо результат у всі 4 слоти кожної 128-бітної половини.
    __m256 dp_f1_t1 = _mm256_dp_ps(v_f1_t1, v_f1_t1, 0x7F);
    __m256 dp_f2_t2 = _mm256_dp_ps(v_f2_t2, v_f2_t2, 0x7F);

    // Готуємо комбінований вектор порогів для порівняння.
    // Оскільки нижні 128 біт — це force (f1/f2), а верхні 128 біт — це torque (t1/t2),
    // ми створюємо відповідний регістр порогів:
    __m256 thresholds = _mm256_set_m128(
        _mm_set1_ps(torque_threshold), // Верхні 128 біт (для t1 та t2)
        _mm_set1_ps(force_threshold)   // Нижні 128 біт (для f1 та f2)
    );

    // Векторизоване порівняння (якщо Dot Product > Threshold)
    __m256 cmp1 = _mm256_cmp_ps(dp_f1_t1, thresholds, _CMP_GT_OQ);
    __m256 cmp2 = _mm256_cmp_ps(dp_f2_t2, thresholds, _CMP_GT_OQ);

    // Зливаємо обидва результати через OR і витягуємо маску
    __m256 cmp_all = _mm256_or_ps(cmp1, cmp2);
    int mask = _mm256_movemask_ps(cmp_all);

    // Якщо хоча б один біт встановлено — з'єднання розірвано
    if (mask != 0) {
        m_breaked = true;
        return true;
    }

    return false;
}
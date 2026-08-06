#include "editor_reference_mesh.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void assert_near(float fActual, float fExpected)
{
    assert(fabsf(fActual - fExpected) <= 0.0001f);
}

static void test_import_copy_normals_and_atomic_failure(const char *szObjPath)
{
    static uint8_t abyTexture[12] = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        99, 99, 99, 99
    };
    tEdReferenceMeshImport Import;
    tEdReferenceMeshState State;
    tEdReferenceMesh Mesh;
    char szError[256];
    tEdReferenceVertex *pCommittedVertices;
    uint8_t *pCommittedTexture;
    eEdReferenceMeshResult eResult;

    ed_reference_mesh_import_init(&Import);
    ed_reference_mesh_state_init(&State);
    eResult = ed_reference_mesh_import_obj(
        szObjPath, &Import, szError, sizeof(szError));
    if (eResult != ED_REFERENCE_MESH_OK)
        fprintf(stderr, "reference OBJ import failed: %s (%s)\n",
                ed_reference_mesh_result_name(eResult), szError);
    assert(eResult == ED_REFERENCE_MESH_OK);
    assert(Import.uiVertexCount == 24u);
    assert(Import.uiIndexCount == 24u);
    assert((Import.uiFlags & ROLLER_ED_REFERENCE_HAS_NORMALS) == 0u);

    memset(&Mesh, 0, sizeof(Mesh));
    Mesh.uiStructSize = sizeof(Mesh);
    Mesh.uiVersion = ROLLER_ED_REFERENCE_MESH_VERSION;
    Mesh.pVertices = Import.pVertices;
    Mesh.uiVertexCount = Import.uiVertexCount;
    Mesh.puiIndices = Import.puiIndices;
    Mesh.uiIndexCount = Import.uiIndexCount;
    Mesh.pbyTextureRGBA = abyTexture;
    Mesh.uiTextureWidth = 2u;
    Mesh.uiTextureHeight = 1u;
    Mesh.uiTextureRowPitch = 12u;
    Mesh.fPosition[0] = 3.0f;
    Mesh.fRotation[1] = 25.0f;
    Mesh.fScale[0] = 1.0f;
    Mesh.fScale[1] = 1.0f;
    Mesh.fScale[2] = 1.0f;
    Mesh.uiFlags = ROLLER_ED_REFERENCE_TWO_SIDED;
    assert(ed_reference_mesh_replace(
        &State, &Mesh, szError, sizeof(szError))
        == ED_REFERENCE_MESH_OK);
    assert(State.uiVertexCount == 24u);
    assert(State.uiIndexCount == 24u);
    assert(State.uiTextureRowPitch == 8u);
    assert((State.uiFlags & ROLLER_ED_REFERENCE_HAS_NORMALS) != 0u);
    assert((State.uiFlags & ROLLER_ED_REFERENCE_TWO_SIDED) != 0u);
    assert_near(State.fPosition[0], 3.0f);
    assert_near(State.fRotation[1], 25.0f);
    assert_near(State.fScale[2], 1.0f);
    assert(fabsf(State.pVertices[0].fNormal[2]) > 0.9f);

    pCommittedVertices = State.pVertices;
    pCommittedTexture = State.pbyTextureRGBA;
    ed_reference_mesh_import_dispose(&Import);
    memset(abyTexture, 0, sizeof(abyTexture));
    assert(State.pVertices == pCommittedVertices);
    assert(State.pbyTextureRGBA == pCommittedTexture);
    assert(State.pbyTextureRGBA[0] == 255u);
    assert(State.pbyTextureRGBA[4] == 0u);
    assert(State.pbyTextureRGBA[5] == 255u);

    {
        const uint32_t auiInvalidIndices[3] = { 0u, 1u, 999u };
        tEdReferenceVertex aVertices[3] = { 0 };

        Mesh.pVertices = aVertices;
        Mesh.uiVertexCount = 3u;
        Mesh.puiIndices = auiInvalidIndices;
        Mesh.uiIndexCount = 3u;
        Mesh.pbyTextureRGBA = NULL;
        Mesh.uiTextureWidth = 0u;
        Mesh.uiTextureHeight = 0u;
        Mesh.uiTextureRowPitch = 0u;
        assert(ed_reference_mesh_replace(
            &State, &Mesh, szError, sizeof(szError))
            == ED_REFERENCE_MESH_INVALID_INDEX);
        assert(State.pVertices == pCommittedVertices);
        assert(State.pbyTextureRGBA == pCommittedTexture);
    }

    Mesh.pVertices = NULL;
    Mesh.uiVertexCount = 0u;
    Mesh.puiIndices = NULL;
    Mesh.uiIndexCount = 0u;
    assert(ed_reference_mesh_replace(
        &State, &Mesh, szError, sizeof(szError))
        == ED_REFERENCE_MESH_OK);
    assert(State.pVertices == NULL);
    assert(State.puiIndices == NULL);
    assert(State.pbyTextureRGBA == NULL);
    ed_reference_mesh_state_dispose(&State);
}

static void test_nonindexed_triangle_is_copied(void)
{
    tEdReferenceVertex aVertices[3] = {
        { .fPosition = { 0.0f, 0.0f, 0.0f } },
        { .fPosition = { 1.0f, 0.0f, 0.0f } },
        { .fPosition = { 0.0f, 1.0f, 0.0f } }
    };
    tEdReferenceMesh Mesh = {
        .uiStructSize = sizeof(Mesh),
        .uiVersion = ROLLER_ED_REFERENCE_MESH_VERSION,
        .pVertices = aVertices,
        .uiVertexCount = 3u,
        .fScale = { 1.0f, 1.0f, 1.0f }
    };
    tEdReferenceMeshState State;
    char szError[128];

    ed_reference_mesh_state_init(&State);
    assert(ed_reference_mesh_replace(
        &State, &Mesh, szError, sizeof(szError))
        == ED_REFERENCE_MESH_OK);
    assert(State.uiIndexCount == 3u);
    assert(State.puiIndices[0] == 0u);
    assert(State.puiIndices[1] == 1u);
    assert(State.puiIndices[2] == 2u);
    aVertices[1].fPosition[0] = 99.0f;
    assert_near(State.pVertices[1].fPosition[0], 1.0f);
    ed_reference_mesh_state_dispose(&State);
}


/* E3A-S7 --------------------------------------------------------------- */

static tEdReferenceMesh make_unit_triangle(tEdReferenceVertex *pVertices)
{
    tEdReferenceMesh Mesh;

    memset(pVertices, 0, sizeof(*pVertices) * 3u);
    pVertices[1].fPosition[0] = 1.0f;
    pVertices[2].fPosition[1] = 1.0f;
    memset(&Mesh, 0, sizeof(Mesh));
    Mesh.uiStructSize = sizeof(Mesh);
    Mesh.uiVersion = ROLLER_ED_REFERENCE_MESH_VERSION;
    Mesh.pVertices = pVertices;
    Mesh.uiVertexCount = 3u;
    Mesh.fScale[0] = 1.0f;
    Mesh.fScale[1] = 1.0f;
    Mesh.fScale[2] = 1.0f;
    return Mesh;
}

static void test_the_current_mesh_is_set_cleared_and_reset(void)
{
    tEdReferenceVertex aVertices[3];
    tEdReferenceMesh Mesh = make_unit_triangle(aVertices);
    char szError[128];

    ed_reference_mesh_reset_current();
    assert(ed_reference_mesh_triangle_count() == 0u);
    assert(!ed_reference_mesh_wireframe());

    assert(ed_reference_mesh_set_current(&Mesh, szError, sizeof(szError))
           == ED_REFERENCE_MESH_OK);
    assert(ed_reference_mesh_triangle_count() == 1u);

    /* The wireframe flag is the host's, read back through the same state. */
    Mesh.uiFlags = ROLLER_ED_REFERENCE_WIREFRAME;
    assert(ed_reference_mesh_set_current(&Mesh, szError, sizeof(szError))
           == ED_REFERENCE_MESH_OK);
    assert(ed_reference_mesh_wireframe());

    /* AD-13: a zero-vertex mesh clears it. */
    Mesh.pVertices = NULL;
    Mesh.uiVertexCount = 0u;
    assert(ed_reference_mesh_set_current(&Mesh, szError, sizeof(szError))
           == ED_REFERENCE_MESH_OK);
    assert(ed_reference_mesh_triangle_count() == 0u);
    ed_reference_mesh_reset_current();
}

static void test_a_failed_replacement_keeps_the_previous_mesh(void)
{
    tEdReferenceVertex aVertices[3];
    tEdReferenceMesh Mesh = make_unit_triangle(aVertices);
    tEdReferenceMesh Broken;
    char szError[128];
    float afTriangle[3][3];

    ed_reference_mesh_reset_current();
    assert(ed_reference_mesh_set_current(&Mesh, szError, sizeof(szError))
           == ED_REFERENCE_MESH_OK);

    /* An index count that is not a multiple of three is not a triangle list. */
    Broken = Mesh;
    Broken.puiIndices = (const uint32_t *)aVertices;
    Broken.uiIndexCount = 2u;
    assert(ed_reference_mesh_set_current(&Broken, szError, sizeof(szError))
           != ED_REFERENCE_MESH_OK);
    assert(ed_reference_mesh_triangle_count() == 1u);
    assert(ed_reference_mesh_world_triangle(0u, afTriangle));
    ed_reference_mesh_reset_current();
}

static void test_the_transform_is_scale_then_rotate_then_translate(void)
{
    tEdReferenceVertex aVertices[3];
    tEdReferenceMesh Mesh = make_unit_triangle(aVertices);
    char szError[128];
    float afTriangle[3][3];

    ed_reference_mesh_reset_current();
    Mesh.fScale[0] = 2.0f;
    Mesh.fScale[1] = 3.0f;
    Mesh.fScale[2] = 4.0f;
    Mesh.fPosition[0] = 10.0f;
    Mesh.fPosition[1] = 20.0f;
    Mesh.fPosition[2] = 30.0f;
    assert(ed_reference_mesh_set_current(&Mesh, szError, sizeof(szError))
           == ED_REFERENCE_MESH_OK);
    assert(ed_reference_mesh_world_triangle(0u, afTriangle));
    /* No rotation: scale then translate, corner by corner. */
    assert_near(afTriangle[0][0], 10.0f);
    assert_near(afTriangle[0][1], 20.0f);
    assert_near(afTriangle[0][2], 30.0f);
    assert_near(afTriangle[1][0], 12.0f);
    assert_near(afTriangle[2][1], 23.0f);

    /* A quarter turn of yaw is about world +Z (up), taking +X to +Y -- the
     * heading convention ADR 0003 records. Scale is applied first, so the
     * moved corner is the scaled one. */
    Mesh.fRotation[0] = 90.0f;
    assert(ed_reference_mesh_set_current(&Mesh, szError, sizeof(szError))
           == ED_REFERENCE_MESH_OK);
    assert(ed_reference_mesh_world_triangle(0u, afTriangle));
    assert_near(afTriangle[1][0], 10.0f);
    assert_near(afTriangle[1][1], 22.0f);
    assert_near(afTriangle[1][2], 30.0f);

    /* Out of range asks for nothing rather than reading past the mesh. */
    assert(!ed_reference_mesh_world_triangle(1u, afTriangle));
    assert(!ed_reference_mesh_world_triangle(0u, NULL));
    ed_reference_mesh_reset_current();
    assert(!ed_reference_mesh_world_triangle(0u, afTriangle));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_import_copy_normals_and_atomic_failure(argv[1]);
    test_nonindexed_triangle_is_copied();
    test_the_current_mesh_is_set_cleared_and_reset();
    test_a_failed_replacement_keeps_the_previous_mesh();
    test_the_transform_is_scale_then_rotate_then_translate();
    puts("editor reference mesh contract tests passed");
    return 0;
}

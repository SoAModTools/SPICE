// Internal MLK, STD, and content-graph operation helpers. Included by OperationExecution.cpp.

int executeMlkCorpus(
    const spice::fileparsing::ExportMlkCorpusRequest& request,
    spice::fileparsing::OperationContext& context) {
    std::filesystem::create_directories(request.output);
    emit(context, spice::fileparsing::EventLevel::Progress,
        "[SpiceFileParsing] Step 2/4: Scanning MLK corpus.");
    const auto corpus = spice::mlk::scanMlkCorpus(request.input);
    emit(context, spice::fileparsing::EventLevel::Progress,
        "[SpiceFileParsing] Step 3/4: Writing MLK corpus artifacts.");
    const auto written = spice::mlk::writeMlkCorpusArtifacts(corpus, request.output);
    emit(context, spice::fileparsing::EventLevel::Info, "[SpiceFileParsing]   - Wrote ", written.jsonPath.string());
    emit(context, spice::fileparsing::EventLevel::Info, "[SpiceFileParsing]   - Wrote ", written.filesCsvPath.string());
    emit(context, spice::fileparsing::EventLevel::Info, "[SpiceFileParsing]   - Wrote ", written.recordsCsvPath.string());
    emit(context, spice::fileparsing::EventLevel::Info, "[SpiceFileParsing]   - Wrote ", written.word12HistogramCsvPath.string());
    emit(context, spice::fileparsing::EventLevel::Info, "[SpiceFileParsing]   - Wrote ", written.anomaliesCsvPath.string());
    emit(context, spice::fileparsing::EventLevel::Info, "[SpiceFileParsing]   - Wrote ", written.word12ByKindCsvPath.string());
    emit(context, spice::fileparsing::EventLevel::Info, "[SpiceFileParsing]   - Wrote ", written.embeddedMldSummaryCsvPath.string());
    emit(context, spice::fileparsing::EventLevel::Progress,
        "[SpiceFileParsing] Step 4/4: Finalizing summary.");
    emit(context, spice::fileparsing::EventLevel::Info, "SpiceFileParsing finished.");
    emit(context, spice::fileparsing::EventLevel::Info, "FilesProcessed=", corpus.files.size());
    emit(context, spice::fileparsing::EventLevel::Info, "input=", request.input.string());
    emit(context, spice::fileparsing::EventLevel::Info, "outputDir=", request.output.string());
    return 0;
}

int executeMlkBlenderIr(
    const spice::fileparsing::ExportMlkBlenderIrRequest& request,
    spice::fileparsing::OperationContext& context) {
    std::filesystem::create_directories(request.output);
    emit(context, spice::fileparsing::EventLevel::Progress,
        "[SpiceFileParsing] Step 2/4: Exporting MLK Blender IR contact sheets.");
    spice::mlk::MlkBlenderIrExportOptions exportOptions{};
    exportOptions.annotationRepositoryDir = request.annotationRepository;
    exportOptions.overwriteMlkAnnotations = request.overwriteAnnotations;
    const auto exported = spice::mlk::exportMlkBlenderIr(request.input, request.output, exportOptions);
    emit(context, spice::fileparsing::EventLevel::Progress,
        "[SpiceFileParsing] Step 3/4: Writing MLK Blender IR artifacts.");
    for (const auto& file : exported.files) {
        emit(context, spice::fileparsing::EventLevel::Info, "[SpiceFileParsing]   - Wrote ", file.combinedBlenderIrPath.string());
        emit(context, spice::fileparsing::EventLevel::Info, "[SpiceFileParsing]   - Wrote ", file.manifestPath.string());
        emit(context, spice::fileparsing::EventLevel::Info, "[SpiceFileParsing]   - Wrote ", file.recordsCsvPath.string());
        if (file.wroteAnnotation) {
            emit(context, spice::fileparsing::EventLevel::Info, "[SpiceFileParsing]   - Wrote ", file.annotationPath.string());
        } else if (file.preservedExistingAnnotation) {
            emit(context, spice::fileparsing::EventLevel::Info,
                "[SpiceFileParsing]   - Preserved existing MLK annotation ", file.annotationPath.string());
        }
        if (file.copiedAnnotationCombinedBlenderIr) {
            emit(context, spice::fileparsing::EventLevel::Info,
                "[SpiceFileParsing]   - Copied annotation Blender IR ", file.annotationCombinedBlenderIrPath.string());
        }
    }
    emit(context, spice::fileparsing::EventLevel::Progress,
        "[SpiceFileParsing] Step 4/4: Finalizing summary.");
    emit(context, spice::fileparsing::EventLevel::Info, "SpiceFileParsing finished.");
    emit(context, spice::fileparsing::EventLevel::Info, "FilesProcessed=", exported.files.size());
    emit(context, spice::fileparsing::EventLevel::Info, "input=", request.input.string());
    emit(context, spice::fileparsing::EventLevel::Info, "outputDir=", request.output.string());
    return 0;
}

